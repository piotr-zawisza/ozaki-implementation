# One-time repository setup on the `max` server

The commands below are run **manually by the user** in a regular interactive session on `max`. If the gated alias from section 4 is set up, open that session with `ssh max-admin` (plain `ssh max` only runs the three whitelisted commands).

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

## 4. Restrict the automation path (optional, recommended)

Goal: the SSH alias used by automation (`max`) can run only the three whitelisted commands, enforced **by the host**, whatever the client sends. Interactive work uses a second alias (`max-admin`) with your normal key. Done once by the user; verified working on 2026-11-04.

1. On the workstation, create a dedicated key: `ssh-keygen -t ed25519 -f ~/.ssh/max_gated -C ozaki-gated`.
2. On `max`, install the gate `~/bin/ozaki-gate` (`chmod 755`):

   ```sh
   #!/bin/sh
   case "$SSH_ORIGINAL_COMMAND" in
     'cd ~/ozaki-max && git pull --ff-only' | \
     'cd ~/ozaki-max && cmake --build build --target ozaki_tests && ctest --test-dir build --output-on-failure' | \
     'cd ~/ozaki-max && ./scripts/run_bench.sh')
       exec bash -c "$SSH_ORIGINAL_COMMAND" ;;
     *)
       echo "ozaki-gate: command not allowed" >&2
       exit 126 ;;
   esac
   ```
3. On `max`, append one line **by hand** to `~/.ssh/authorized_keys` (not `ssh-copy-id`, which would add the key unrestricted):
   `command="/home/<user>/bin/ozaki-gate",restrict <contents of max_gated.pub>`
4. Test with the key given explicitly, before changing any config: a non-whitelisted command and an interactive login must both print `command not allowed`; a whitelisted one must run.
5. In the workstation `~/.ssh/config`, rename the existing `Host max` block to `Host max-admin` and add:

   ```
   Host max
       HostName <hostname>
       User <user>
       IdentityFile ~/.ssh/max_gated
       IdentitiesOnly yes
       ControlMaster no
       ControlPath none
       ForwardAgent no
       ForwardX11 no
   ```
6. Verify: `ssh max true` is refused, the whitelisted commands run, `ssh max-admin` gives a normal shell.

Rollback: restore the saved copy of `~/.ssh/config`, or delete the key's line from `~/.ssh/authorized_keys`.

The gate is a fixed copy of the whitelist: **if the whitelist changes** (CLAUDE.md rule 5), update the `case` list in `~/bin/ozaki-gate` as well, or the new command is refused by the host. The gate inherits the same non-interactive environment as before (see the notes below), so `cmake`/`ctest` are found as long as `PATH` is set before the `~/.bashrc` guard.

## Notes

- **X11 message.** `restrict` disables X11 forwarding, so a client with `ForwardX11 yes` prints `X11 forwarding request failed on channel 0`. It is harmless; `ForwardX11 no` on the `max` alias removes it.
- **Non-interactive shell.** A remote command such as `ssh max "..."` does not read the part of `~/.bashrc` that follows the guard `[[ $- != *i* ]] && return`. If `cmake`/`g++` come from `module load`, that line must be placed in `~/.bashrc` **before** the guard (or in `~/.bash_profile`/`~/.profile`), otherwise the remote build will not find `cmake`/`ctest`.
- **cmake on `max`.** There is no system `cmake`; the official tarball is installed under `~/.local`, and the line that adds its `bin` directory to `PATH` sits in `~/.bashrc` before that guard, so remote commands find `cmake`/`ctest`.
- **HBM nodes.** Confirmed with `numactl -H` on `max` (2026-11-02): 64 CPUs in 8 SNC4 nodes 0–7 (DDR, ~64 GB each); HBM is nodes 8–15 (no CPUs, 16 GB each); the nearest HBM node for CPU node N is N+8. `scripts/run_bench.sh` binds to `8-15` by default (override with `OZAKI_HBM_NODES`) and `--numa-report` prints the page placement.
- **Updates.** After every `git push` from the local machine: remote pull, then build + ctest.
