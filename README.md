# scx for Linux 6.1

Backport of the [sched-ext/scx](https://github.com/sched-ext/scx) schedulers to
the Linux 6.1 `sched_ext` backport used by Android 14 GKI devices.

This repository is dedicated to the 6.1 target. It is a fork of scx at
upstream commit `9173d645`, with the 6.1 work kept in a small set of commits
on top.

Status: experimental. Tested on:

- Android 14 / Linux 6.1.145 (arm64 device)
- Cuttlefish / Linux 6.1.175 (x86_64)

## What is here

| Path | Description |
|---|---|
| `scheds/k6.1/scx_lavd` | `scx_lavd_k61` — LAVD ported to 6.1 |
| `scheds/k6.1/scx_p2dq` | `scx_p2dq_k61` — P2DQ ported to 6.1 |
| `scheds/rust/scx_fcp` | `scx_fcp` — frame critical-path scheduler (vendored) |
| `scheds/k6.1/include` | 6.1 sched_ext/BPF compatibility headers |
| `kernel/` | Kernel-side backport patch and notes |
| `rust/scx_utils` | Shared userspace library, with 6.1 runtime fixes |

The upstream schedulers that are not part of the 6.1 port are kept in the
tree for reference and for rebasing, but are not built by CI.

## Kernel requirements

The kernel must carry the backport in `kernel/0001-scx-6.1-minimal-backport.patch`
and be built with:

```
CONFIG_BPF=y
CONFIG_BPF_SYSCALL=y
CONFIG_BPF_JIT=y
CONFIG_BPF_JIT_ALWAYS_ON=y
CONFIG_BPF_JIT_DEFAULT_ON=y
CONFIG_DEBUG_INFO_BTF=y
CONFIG_SCHED_CLASS_EXT=y
CONFIG_KALLSYMS_ALL=y
CONFIG_FUNCTION_TRACER=y       # scx_lavd futex tracking
CONFIG_FTRACE_SYSCALLS=y
CONFIG_IKCONFIG_PROC=y         # optional, lets /proc/config.gz verify the above
```

`CONFIG_DYNAMIC_FTRACE` follows `CONFIG_FUNCTION_TRACER` (`default y`).

### BTF and pahole

`CONFIG_DEBUG_INFO_BTF` requires pahole at kernel build time. pahole 1.23
(used by some Android 14 prebuilts) does not emit `BTF_KIND_ENUM64`, so
64-bit sched_ext enumerators are truncated in `/sys/kernel/btf/vmlinux`.

The 6.1 binaries select the `Linux61` enum ABI through
`scx_utils::compat::set_enum_abi()` and recover the values from
`rust/scx_utils/src/enums_abi.k61.rs`. The scheduler logs a warning once;
this is expected with pahole 1.23. pahole >= 1.24 removes the warning and
is recommended for new kernels.

## Build

Host dependencies:

```sh
sudo apt install clang libelf-dev zlib1g-dev libzstd-dev pkg-config
```

Rust: latest stable (the vendored `scx_fcp` uses edition 2026).

Native build (x86_64 host, useful as a smoke test):

```sh
cargo build --release -p scx_lavd_k61 -p scx_p2dq_k61 -p scx_fcp
```

Cross build for an arm64 device:

```sh
# Static arm64 libs used by libbpf: libelf.a, libz.a, libzstd.a
export ARM64_LIBS=$HOME/opt/arm64-libs

CARGO_TARGET_AARCH64_UNKNOWN_LINUX_GNU_LINKER=aarch64-linux-gnu-gcc \
RUSTFLAGS="-C target-feature=+crt-static \
           -C link-arg=-L$ARM64_LIBS/lib \
           -C link-arg=$ARM64_LIBS/lib/libzstd.a" \
cargo build --release --target aarch64-unknown-linux-gnu \
  -p scx_lavd_k61 -p scx_p2dq_k61 -p scx_fcp
```

Binaries land in `target/<target-triple>/release/`.

## Run

Android has no `/var/run`, so point the stats socket at a writable
directory and run as root:

```sh
adb push target/aarch64-unknown-linux-gnu/release/scx_lavd_k61 /data/local/tmp/scx/
adb shell 'su 0 chmod 755 /data/local/tmp/scx/scx_lavd_k61'
adb shell 'su 0 sh -c "SCX_STATS_BASE_PATH=/data/local/tmp/scx \
    /data/local/tmp/scx/scx_lavd_k61"'
```

Check the state from another shell:

```sh
adb shell 'su 0 cat /sys/kernel/sched_ext/state'
adb shell 'su 0 cat /sys/kernel/sched_ext/root/ops'
```

The state file is root-only on Android, hence `su 0`.

## Known limitations

- **Measurement caveat**: the 6.1 backport hooks the CFS path in
  `kernel/sched/fair.c` instead of using a dedicated `ext_sched_class`.
  Results are therefore not directly comparable to upstream sched_ext on
  6.12+.
- `scx_lavd_k61` loses futex-holder boosting when the kernel is built
  without `CONFIG_FUNCTION_TRACER`/`CONFIG_FTRACE_SYSCALLS`.
- pahole 1.23 kernels log the `BTF_KIND_ENUM64` warning described above.
- After `SIGKILL` of a scheduler process, `/sys/kernel/sched_ext/state`
  may remain `enabled`; reboot the device before switching schedulers.
- The kernel backport raises `BPF_COMPLEXITY_LIMIT_INSNS` to 4000000 for
  the merged-TU `scx_lavd` object. `MAX_CALL_FRAMES` stays at the upstream
  value 8.

## Upstream sync

The 6.1 changes are confined to:

- `scheds/k6.1/`
- `scheds/rust/scx_fcp/` (vendored; upstream is a separate repository)
- `rust/scx_utils/` and `rust/scx_cargo/` compatibility code
- `kernel/`

Everything else tracks `sched-ext/scx`. Rebase the 6.1 branch on upstream
`main` periodically; the port does not modify the upstream schedulers.

## License

GPL-2.0-only, following the upstream scx project.
