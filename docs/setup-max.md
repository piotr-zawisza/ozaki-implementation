# Konfiguracja repo na serwerze `max` (jednorazowo)

Komendy poniżej uruchamia **użytkownik ręcznie** w zwykłej sesji ssh na `max`.

Repo jest publiczne, więc klonowanie po HTTPS nie wymaga poświadczeń.

## 0. Sprawdzenie narzędzi

```bash
git --version
cmake --version                                   # >= 3.21 (inaczej: module load cmake albo pip install --user cmake)
g++ --version                                     # >= 11 (-march=sapphirerapids)
grep -o 'amx_[a-z0-9]*' /proc/cpuinfo | sort -u   # oczekiwane: amx_bf16 amx_int8 amx_tile
numactl -H                                        # węzły NUMA; HBM w trybie flat to węzły bez CPU
```

## 1. Klon

```bash
[ -e ~/ozaki-max ] && mv ~/ozaki-max ~/ozaki-max.bak.$(date +%s)
git clone --branch main https://github.com/piotr-zawisza/ozaki-implementation.git ~/ozaki-max
cd ~/ozaki-max
git config pull.ff only
```

Zdalna komenda `git pull --ff-only` nie robi `checkout`, więc klon musi stać na `main` (tak jest po `--branch main`).

## 2. Konfiguracja buildu (raz)

Zdalna komenda budująca zakłada, że `build/` już istnieje i jest skonfigurowany.

```bash
cd ~/ozaki-max
cmake --preset max        # = cmake -B build -DCMAKE_BUILD_TYPE=Release -DOZAKI_NATIVE_ARCH=ON
```

## 3. Kontrola (to samo, co dozwolone komendy zdalne)

```bash
cd ~/ozaki-max
cmake --build build --target ozaki_tests && ctest --test-dir build --output-on-failure
./scripts/run_bench.sh
```

## Uwagi

- **Powłoka nieinteraktywna.** `ssh max "..."` nie czyta części `~/.bashrc` za strażnikiem `[[ $- != *i* ]] && return`. Jeśli `cmake`/`g++` pochodzą z `module load`, wpis musi stać w `~/.bashrc` **przed** tym strażnikiem (albo w `~/.bash_profile`/`~/.profile`), inaczej zdalny build nie znajdzie `cmake`/`ctest`. Sprawdź: `ssh max 'command -v cmake'` jako zwykły użytkownik.
- **Węzły HBM.** `scripts/run_bench.sh` ma `--membind=0,1,2,3`. Na Xeon Max w trybie flat z SNC4 węzły 0–3 to zwykle DDR, a HBM to 4–7 (bez CPU). Potwierdź przez `numactl -H` i popraw skrypt osobną zmianą.
- **Aktualizacje.** Po każdym `git push` z lokalnej maszyny: zdalny pull, potem build+ctest (każde za zgodą w bieżącej turze).
