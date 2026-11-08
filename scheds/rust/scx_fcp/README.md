# scx_fcp on Linux 6.1

This directory contains the tuned `scx_fcp` scheduler ported to the Linux 6.1
sched_ext backport. The generic design, options, and telemetry are documented
in the mainline `scx_fcp` tree; this page lists only the 6.1 differences and
the commands needed to build and test this tree.

## 6.1 differences

- `build.rs` always defines `SCX_FCP_ANDROID_61`. The same define selects the
  6.1 compatibility branches in the BPF headers and disables the newer
  DSQ-insert compat stubs.
- The 6.1 compat layer stubs `scx_bpf_dsq_insert*()` under that define. The BPF
  code therefore routes insertion through the backport's
  `scx_bpf_dispatch*___compat()` wrappers.
- The 6.1 backport does not emit `SCX_WAKE_SYNC`. Token inheritance still uses
  the bounded same-TGID wakeup window, so synchronous handoffs are admitted
  without the explicit flag.
- `scx_bpf_task_set_dsq_vtime___new()` is not present. The scheduler passes
  vtime when inserting into the critical and interactive DSQs; there is no
  in-task write.
- The backport hooks the CFS path instead of adding a dedicated
  `ext_sched_class`. These results are a separate 6.1 set and are not directly
  comparable with the mainline kernel results.
- The build uses edition 2024 and Rust 1.91 or newer.

## Build

Build the default, named, and zero-hint profiles plus `fcp_frame_bench`:

```bash
./scripts/build-6.1-fcp.sh
```

Outputs are written to `build/6.1-fcp/bin`:

| Binary | Profile | Ops |
|---|---|---|
| `scx_fcp` | default tuned | `fcp` |
| `scx_fcp_named` | named roots | `fcp_nh` |
| `scx_fcp_behavioral` | zero-hint | `fcp_zh` |
| `fcp_frame_bench` | synthetic benchmark | n/a |

Build the 6.1 LAVD and P2DQ baselines as well:

```bash
BUILD_BASELINES=1 ./scripts/build-6.1-fcp.sh
```

The baseline binaries are installed as `scx_lavd` and `scx_p2dq` for the test
harness. Their native names are `scx_lavd_k61` and `scx_p2dq_k61`.

Cross build:

```bash
TARGET=aarch64-unknown-linux-gnu CC=aarch64-linux-gnu-gcc \
  ./scripts/build-6.1-fcp.sh
```

Set the usual `CARGO_TARGET_AARCH64_UNKNOWN_LINUX_GNU_LINKER` and `RUSTFLAGS`
for the target; see the top-level `README.md`.

## Run

Android has no `/var/run`. Use a writable stats path and run as root:

```bash
adb shell 'su 0 sh -c "SCX_STATS_BASE_PATH=/data/local/tmp/scx \
    /data/local/tmp/scx/scx_fcp -f 16667 -s 2"'
```

Check the attach state:

```bash
adb shell 'su 0 cat /sys/kernel/sched_ext/state'
adb shell 'su 0 cat /sys/kernel/sched_ext/root/ops'
```

The scheduler options are the same as the mainline tuned build. Do not pass
`--frame-pids` to the zero-hint binary.

## Tests

The shared JankBench, UiBench, and synthetic harnesses live in `tests/`:

```bash
tests/jankbench/run_jankbench.sh -p eevdf -n 1
tests/uibench/run_uibench.sh -p scx_fcp -n 1 -r 3
tests/synthetic/run_fcp_frame_bench.sh
```

See [`tests/README.md`](../../../tests/README.md) for result layout, device
variables, and validity rules.

## Kernel and ABI

The kernel-side backport, config, and pahole notes are in
[`kernel/README.md`](../../../kernel/README.md). The 6.1 vmlinux/ABI tables are
under `scheds/include/vmlinux/` and `scheds/k6.1/include/`; regenerate them only
against the kernel that will run the benchmark.
