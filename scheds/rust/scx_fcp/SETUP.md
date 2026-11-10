# scx_fcp setup on Linux 6.1

This guide covers only the 6.1-specific build, kernel, device, and test steps.
Common host prerequisites and the scheduler options are documented in the
mainline `scx_fcp` tree.

## 1. Build

The 6.1 tree builds the scheduler against the vendored 6.1 ABI tables. Build
the tuned profiles and the synthetic benchmark with:

```bash
cd /path/to/scx-6.1
./scripts/build-6.1-fcp.sh
```

Outputs:

```text
build/6.1-fcp/bin/scx_fcp
build/6.1-fcp/bin/scx_fcp_named
build/6.1-fcp/bin/scx_fcp_behavioral
build/6.1-fcp/bin/fcp_frame_bench
```

Add the 6.1 baselines with `BUILD_BASELINES=1`. They are installed as
`scx_lavd` and `scx_p2dq`, backed by `scx_lavd_k61` and `scx_p2dq_k61`.

For an ARM64 device, set the cross linker and target. The top-level README
has the static `RUSTFLAGS` and `ARM64_LIBS` recipe; the helper accepts the
target and C compiler:

```bash
TARGET=aarch64-unknown-linux-gnu CC=aarch64-linux-gnu-gcc \
  ./scripts/build-6.1-fcp.sh
```

Do not set `SCX_FCP_ANDROID_61` manually. `build.rs` defines it for this tree;
without it the 6.1 compatibility branches and dispatch path are not selected.

## 2. Kernel

The target kernel must carry the backport patch in `kernel/`. Apply it to the
kernel tree that produces the running image:

```bash
git am /path/to/scx-6.1/kernel/0001-scx-6.1-minimal-backport.patch
```

Required config and pahole/BTF notes are in [`kernel/README.md`](../../../kernel/README.md).
At minimum:

```text
CONFIG_BPF=y
CONFIG_BPF_SYSCALL=y
CONFIG_BPF_JIT=y
CONFIG_BPF_JIT_ALWAYS_ON=y
CONFIG_BPF_JIT_DEFAULT_ON=y
CONFIG_DEBUG_INFO_BTF=y
CONFIG_SCHED_CLASS_EXT=y
CONFIG_KALLSYMS_ALL=y
CONFIG_IKCONFIG_PROC=y
```

The backport hooks the CFS path, so 6.1 results are a separate set from
mainline sched_ext results.

## 3. SA8155P Android 14 device

Stage the scheduler and benchmark under `/data/local/tmp/scx`; use `su 0` on
the device. SELinux must be permissive on the test image.

```bash
adb -s "$SERIAL" push build/6.1-fcp/bin/. /data/local/tmp/scx/
adb -s "$SERIAL" shell 'su 0 chmod 755 /data/local/tmp/scx/scx_* /data/local/tmp/scx/fcp_frame_bench'
adb -s "$SERIAL" shell 'su 0 setenforce 0'
adb -s "$SERIAL" shell 'su 0 sh -c "
  ulimit -l unlimited
  SCX_STATS_BASE_PATH=/data/local/tmp/scx/stats \
    nohup /data/local/tmp/scx/scx_fcp -f 16667 -s 2 \
    > /data/local/tmp/scx_fcp.log 2>&1 < /dev/null &"'
```

Verify the attach:

```bash
adb -s "$SERIAL" shell 'su 0 cat /sys/kernel/sched_ext/state'
adb -s "$SERIAL" shell 'su 0 cat /sys/kernel/sched_ext/root/ops'
```

The expected ops name is `fcp` for the default tuned profile, `fcp_nh` for
named, and `fcp_zh` for behavioral.

## 4. Tests

The test harnesses are shared with the mainline tree and adapted for the 6.1
binary names. From the `scx-6.1` root:

```bash
BIN_DIR=build/6.1-fcp/bin \
  tests/jankbench/run_jankbench.sh -p scx_fcp -n 1

BIN_DIR=build/6.1-fcp/bin \
  tests/uibench/run_uibench.sh -p scx_fcp -n 1 -r 3

BIN_DIR=build/6.1-fcp/bin SCHEDULER_LABEL=scx_fcp \
  tests/synthetic/run_fcp_frame_bench.sh
```

JankBench and UiBench need the workload APKs on the device; see
[`tests/README.md`](../../../tests/README.md). The test policy names map
`scx_lavd` to `scx_lavd_k61` and `scx_p2dq` to `scx_p2dq_k61`.

## 5. Cleanup and caveat

Stop the scheduler and verify the state:

```bash
adb -s "$SERIAL" shell 'su 0 pkill -f /data/local/tmp/scx/scx_fcp'
adb -s "$SERIAL" shell 'su 0 cat /sys/kernel/sched_ext/state'
```

If a scheduler is killed with `SIGKILL`, the 6.1 backport can leave
`/sys/kernel/sched_ext/state` at `enabled`. Reboot the device before switching
schedulers or starting a new policy block.
