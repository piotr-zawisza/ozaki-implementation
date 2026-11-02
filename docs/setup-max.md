# One-time repository setup on the `max` server

The commands below are run **manually by the user** in a regular interactive session on `max`.

The repository is public, so cloning over HTTPS needs no credentials.

## 0. Check the tools

```bash
git --version
cmake --version                                   # >= 3.21 (otherwise: module load cmake or pip install --user cmake)
g++ --version                                     # >= 11 (-march=sapphirerapids)
grep -o 'amx_[a-z0-9]*' /proc/cpuinfo | sort -u   # expected: amx_bf16 amx_int8 amx_tile
numactl -H                                        # NUMA nodes; HBM in flat mode = nodes without CPUs
```

## 1. Clone

```bash
[ -e ~/ozaki-max ] && mv ~/ozaki-max ~/ozaki-max.bak.$(date +%s)
git clone --branch main https://github.com/piotr-zawisza/ozaki-implementation.git ~/ozaki-max
cd ~/ozaki-max
git config pull.ff only
```

A remote `git pull --ff-only` does not run `checkout`, so the clone must sit on `main` (it does after `--branch main`).

## 2. Configure the build (once)

The remote build command assumes `build/` already exists and is configured.

```bash
cd ~/ozaki-max
cmake --preset max        # = cmake -B build -DCMAKE_BUILD_TYPE=Release -DOZAKI_NATIVE_ARCH=ON
```

## 3. Verify

```bash
cd ~/ozaki-max
cmake --build build --target ozaki_tests && ctest --test-dir build --output-on-failure
./scripts/run_bench.sh
```

## Notes

- **Non-interactive shell.** A remote command such as `ssh max "..."` does not read the part of `~/.bashrc` that follows the guard `[[ $- != *i* ]] && return`. If `cmake`/`g++` come from `module load`, that line must be placed in `~/.bashrc` **before** the guard (or in `~/.bash_profile`/`~/.profile`), otherwise the remote build will not find `cmake`/`ctest`.
- **HBM nodes.** `scripts/run_bench.sh` uses `--membind=0,1,2,3`. On Xeon Max in flat mode with SNC4, nodes 0–3 are usually DDR and HBM is 4–7 (no CPUs). Confirm with `numactl -H` and fix the script in a separate change.
- **Updates.** After every `git push` from the local machine: remote pull, then build + ctest.
