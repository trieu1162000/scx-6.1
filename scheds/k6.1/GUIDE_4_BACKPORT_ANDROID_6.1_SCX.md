# Guide 4: Backport `sched_ext` to Android 6.1, Build, Boot, and Run `scx`

This guide describes a **staged, reversible** path for adding `sched_ext` to
the Android 6.1 kernel used by the Renault EGG platform. It is intentionally
different from the Android 6.18 migration guide: the target is the existing
Android 6.1 vendor kernel, and Qualcomm vendor modules must remain compatible
with the exact kernel/KMI that is shipped on the device.

## 1. Verified workspace state

The relevant Android source tree is:

```text
/home/worker/renault_u_release/vendor_android/kernel_platform
```

The top-level build configuration selects:

```text
KERNEL_DIR=./msm-kernel
MSM_ARCH=gen3egg
VARIANT=gki
```

Therefore, the kernel source actually compiled by the standard Android build
is `kernel_platform/msm-kernel`, not `kernel_platform/common`. The two
directories are separate Git repositories.

The current checkout has these properties:

| Item | Current observation |
|---|---|
| `common` branch | `ctabs_final` |
| `common` working tree | Has pre-existing local scheduler/Binder changes |
| `msm-kernel` working tree | Clean at the time this guide was written |
| `common` reported kernel version | `6.1.145` |
| `CONFIG_SCHED_CLASS_EXT` on `ctabs_final` | Not present in the inspected source/configuration |
| Historical backport branch | `feat/scx-6.1-gki` in `common`, tip `7e39a4e07757` |
| Verified 6.1 `sched_ext` backport | **Yes (`feat/scx-6.1-gki` checked out in `common`)** |
| Historical Android `boot.img` build | **Yes, logged for `aivi2_r_full_egg` and `aivi2_r_da_aub`** |
| Verified 6.1 boot/runtime | **Yes — booted and verified on physical `aivi2_r_full_egg` (`88249622`, `6.1.145-android14-11-maybe-dirty`)** |
| Verified ARM64 `scx` status | **`scx_fcp` (`1.1.4 aarch64`) attaches and runs cleanly on physical `88249622` (`UserExitInfo { kind: 0, exit_code: 0 }`)** |

Ada logs record the following backport commits on `feat/scx-6.1-gki`:
`56340cf64edc`, `25c9b15103b5`, and `7e39a4e07757`. They also record
`out/target/product/aivi2_r_full_egg/boot.img` and
`out/target/product/aivi2_r_da_aub/boot.img` at approximately 98 MiB, built on
November 1, 2026 at 00:21. Those exact product images are not present in the
current filesystem, so their hashes, embedded configuration, and matching
module images cannot now be verified from the artifacts alone.

Do not overwrite or reset the existing `common` working tree. In particular,
the local changes in `common` are not automatically included when the standard
`gen3egg` build uses `msm-kernel`.

## 2. Preconditions and safety rules

1. Use a disposable branch for the backport. Do not patch the `common`
   checkout first.
2. Record the exact source commits of both `common` and `msm-kernel`.
3. Use a pinned `sched_ext` patch series. Do not copy only
   `kernel/sched/ext.c` from a newer kernel.
4. Do not replace the complete 6.1 kernel with the 6.18 tree. That changes
   kernel interfaces and can invalidate Qualcomm `.ko` files, AVB artifacts,
   device trees, and vendor boot assumptions.
5. Do not flash a physical device until the new kernel has booted in a
   disposable/emulator target and all required modules have been rebuilt.
6. Keep the original boot slot and original images available for recovery.

## 3. Capture a clean baseline

Run this before applying any backport:

```bash
export KP=/home/worker/renault_u_release/vendor_android/kernel_platform
export KERNEL_SRC="$KP/msm-kernel"

cd "$KP"

git -C common status --short
git -C common branch --show-current
git -C common rev-parse HEAD
make -C common -s kernelversion

git -C msm-kernel status --short
git -C msm-kernel branch --show-current
git -C msm-kernel rev-parse HEAD
make -C msm-kernel -s kernelversion

rg -n 'CONFIG_SCHED_CLASS_EXT|SCHED_CLASS_EXT|sched_ext' \
  "$KERNEL_SRC" --glob '!Documentation/**' --glob '!tools/**' || true
```

Save the output with the build log. A successful backport report must identify
the exact `msm-kernel` commit, not only the Android release name.

The standard Android build already selects `kernel_platform/msm-kernel`.
Because the build configuration uses that path directly, create a dedicated
branch in the clean `msm-kernel` checkout rather than applying the patch to an
unrelated worktree:

```bash
cd "$KP"
export BASE_MSM_COMMIT="$(git -C msm-kernel rev-parse HEAD)"
export BASE_MSM_REF="$(git -C msm-kernel symbolic-ref --short -q HEAD || true)"

git -C msm-kernel switch -c backport/sched-ext-6.1 "$BASE_MSM_COMMIT"
export SCX_KERNEL_SRC="$KP/msm-kernel"
```

If `msm-kernel` is not clean, stop and preserve those changes before
continuing. Do not use `git reset --hard` to make the source appear clean.

## 4. Select and apply the backport series

### 4.1. Pin the source series

The backport must come from a known Linux version or a maintained Android
backport branch whose `sched_ext` user-space ABI is documented. Record:

```text
source repository:
source tag or commit:
patch range:
scx checkout commit:
```

The series normally includes more than the scheduler implementation. At
minimum, audit changes affecting:

- scheduler core and scheduler-class ordering;
- `include/linux/sched/ext.h` and related scheduler headers;
- UAPI declarations used by the loader;
- BPF struct-ops registration and lifecycle;
- BPF verifier, kfunc, and BTF support;
- `tools/include`, libbpf, and generated `vmlinux.h` assumptions;
- Android Kconfig and build integration.

Do not declare the series usable merely because it applies with `git am`.
Compile conflicts and semantic conflicts are expected on a vendor 6.1 tree.

### 4.2. Apply it in the disposable branch

For a prepared patch series:

```bash
cd "$SCX_KERNEL_SRC"
git am --3way /path/to/sched-ext-6.1-backport/*.patch
```

If a patch conflicts in scheduler code:

```bash
git status
git am --show-current-patch=diff
# Resolve the conflict manually, then:
git add <resolved-files>
git am --continue
```

Never resolve scheduler conflicts by taking the entire newer file. Preserve
the Android/Qualcomm changes and review every hunk in:

```text
kernel/sched/core.c
kernel/sched/fair.c
kernel/sched/sched.h
include/linux/sched.h
include/linux/wait.h
kernel/bpf/
include/linux/bpf.h
include/uapi/
```

The existing `common` repository also contains local scheduler/Binder work.
If that work is required by the target device, port it separately and
document which repository is authoritative for each resulting file. Do not
silently assume that a change in `common` is built from `msm-kernel`.

### 4.3. Required semantic review

Before enabling the feature, inspect the resulting code for:

- the insertion point and invariants of `ext_sched_class`;
- wakeup, enqueue, dequeue, migration, and CPU-selection paths;
- preemption and `pick_next_task()` handling;
- idle CPU and dispatch-queue behavior;
- task exit, fork, cgroup, hotplug, and CPU-offline paths;
- interaction with Android Binder/CTABS scheduler changes;
- BPF struct-ops registration and teardown;
- kfunc availability and BTF type IDs;
- module and KMI effects.

The backport is not complete if `scx_fcp` compiles but the scheduler core
paths have not been reviewed against the Android 6.1 changes.

## 5. Add a reproducible Android configuration

The `gen3egg` GKI configuration is selected by:

```text
msm-kernel/arch/arm64/configs/vendor/gen3egg_GKI.config
```

First verify that the backported Kconfig symbols exist:

```bash
cd "$SCX_KERNEL_SRC"
rg -n 'config SCHED_CLASS_EXT|config BPF_SYSCALL|config BPF_JIT|config DEBUG_INFO_BTF' \
  init kernel arch
```

Only after those symbols exist, add the required settings to the appropriate
version-controlled `gen3egg` fragment or to a separate fragment wired into the
Android build:

```text
CONFIG_BPF=y
CONFIG_BPF_SYSCALL=y
CONFIG_BPF_JIT=y
CONFIG_DEBUG_INFO_BTF=y
CONFIG_DEBUG_INFO_BTF_MODULES=y
CONFIG_SCHED_CLASS_EXT=y
CONFIG_EXT_GROUP_SCHED=y
```

If the backported tree exposes `CONFIG_SCHED_PROXY_EXEC`, disable it for the
initial bring-up:

```text
# CONFIG_SCHED_PROXY_EXEC is not set
```

Do not add a symbol that is absent from Kconfig. Do not edit only the generated
`out/**/.config`; `build/build.sh` regenerates it.

`CONFIG_DEBUG_INFO_BTF=y` requires a working `pahole`/BTF toolchain. Verify the
final generated configuration rather than trusting the fragment:

```bash
grep -E 'CONFIG_(BPF|BPF_SYSCALL|BPF_JIT|DEBUG_INFO_BTF|SCHED_CLASS_EXT|EXT_GROUP_SCHED|SCHED_PROXY_EXEC)' \
  "$OUT_DIR/.config"
```

### 5.1. Android 14/GKE COS BTF enum-width compatibility

Android 14's prebuilt `kernel-build-tools` uses `pahole` v1.23. GKE COS has
the same compatibility issue on affected builds. `BTF_KIND_ENUM64` requires
`pahole` >= 1.24; with v1.23 (or when `--skip_encoding_btf_enum64` is passed),
the 8-byte `scx_*` enums are emitted as `BTF_KIND_ENUM` with `size = 8`, but
their upper 32 bits are truncated to zero. The affected types are:

```text
scx_dsq_id_flags
scx_enq_flags
scx_deq_flags
scx_public_consts
```

Do not modify `rust/scx_utils/src/enums.rs` to work around this. In the
backported kernel, add the following type holder in `kernel/sched/ext.c` so
Clang emits the enum types into DWARF/BTF:

```c
struct __scx_btf_type_holder {
	enum scx_dsq_id_flags dsq_id_flags;
	enum scx_enq_flags enq_flags;
	enum scx_deq_flags deq_flags;
	enum scx_public_consts public_consts;
};

BTF_TYPE_EMIT(struct __scx_btf_type_holder);
```

Then use upstream `scx` commit `596dcf4f`, which adds
`recover_truncated_enum64()` in `rust/scx_utils/src/compat.rs`. `scx_utils`
automatically restores the complete 64-bit values from
`enums_abi.autogen.rs`; `enums.rs` remains unchanged.

### 5.2. Required out-of-tree fixes in `kernel_platform/common`

These are K6.1 backport fixes, not replacements for the main `sched_ext`
patch series:

- `kernel/sched/fair.c` (`f19763616288`): at the top of both
  `enqueue_task_fair()` and `dequeue_task_fair()`, save
  `int orig_flags = flags;` before the `for_each_sched_entity(se)` loop
  overwrites `flags`. Pass `orig_flags` to `scx_enqueue_task_fair()` and
  `scx_dequeue_task_fair()`.
- `kernel/sched/ext.c` (`1775b709f578`): make `__scx_do_insert()` honor
  custom user DSQs through `scx_find_user_dsq_slot()` when called from
  `ops.select_cpu()`, and check `cpumask_test_cpu(tcpu, p->cpus_ptr)` for
  `SCX_DSQ_LOCAL_ON`.

The struct-ops link teardown fix (`kernel/bpf/bpf_struct_ops.c`,
`56340cf64edc`) already calls
`bpf_struct_ops_map_delete_elem(st_link->map, &zero)` when `drop(struct_ops)`
closes `link_fd`. Therefore, do not add `bpf_struct_ops_get()` to link
creation, and do not add `skel.maps.fcp_ops.delete(&0u32)` to `main.rs`.

### 5.3. Guard `cid.bpf.h` for Android 6.1

In `scheds/k6.1/include/scx/common.bpf.h`, wrap the include as follows:

```c
#ifndef SCX_FCP_ANDROID_61
#include "cid.bpf.h"
#endif
```

`cid.bpf.h` defines four non-static `__weak` global BPF subprograms:
`cmask_and`, `cmask_or`, `cmask_andnot`, and `cmask_copy`. Their parameters
use `struct scx_cmask __arena __arg_arena *`. Because they are non-static
globals, Clang emits them into `.text` and BTF `FUNC` records in every
`.bpf.o` that includes `common.bpf.h`, even when unused. Linux 6.1 lacks
`BPF_MAP_TYPE_ARENA` and `arg:arena` verifier support, so it rejects those
records. The guard prevents the unsupported declarations from entering
Android 6.1 objects.

### 5.4. Why only `scx_fcp` can load on Linux 6.1

`scx_lavd_k61` and `scx_p2dq_k61` depend on features newer than Linux 6.1:

- `BPF_MAP_TYPE_ARENA` and `__arena` address-space pointers via `scx_arena`
  (added in Linux 6.9);
- open-coded numeric iterators, `bpf_for()`, and the
  `bpf_iter_num_new/next/destroy` kfuncs (added in Linux 6.4);
- `bpf_iter_scx_dsq_*` DSQ iterators.

They may still compile as part of a cross-build, but they cannot load on this
6.1 target. `scx_fcp` avoids all of these features: it uses
`BPF_MAP_TYPE_TASK_STORAGE`, `BPF_MAP_TYPE_ARRAY`, and verifier-unrolled
`#pragma unroll` loops.

## 6. Build the Android 6.1 kernel and modules

Use the existing Android build entry point. The target-specific build config
already enables the `gen3egg` layout, boot image generation, and vendor DLKM
generation.

```bash
cd "$KP"

export OUT_DIR="$KP/out/scx-6.1-gen3egg-gki"
export BUILD_CONFIG=./build.config
export VARIANT=gki

BUILD_CONFIG="$BUILD_CONFIG" \
VARIANT="$VARIANT" \
OUT_DIR="$OUT_DIR" \
  ./build/build.sh 2>&1 | tee "$KP/scx-6.1-gen3egg-gki.build.log"
```

For an incremental rebuild after the first successful configuration:

```bash
cd "$KP"
BUILD_CONFIG=./build.config \
VARIANT=gki \
OUT_DIR="$OUT_DIR" \
SKIP_MRPROPER=1 \
  ./build/build.sh 2>&1 | tee -a "$KP/scx-6.1-gen3egg-gki.build.log"
```

Expected output locations are:

```text
$OUT_DIR/msm-kernel/.config
$OUT_DIR/dist/Image
$OUT_DIR/dist/Image.lz4
$OUT_DIR/dist/vmlinux
$OUT_DIR/dist/boot.img
$OUT_DIR/dist/vendor_dlkm.img
```

The exact set of images depends on the target build configuration. Treat the
build as failed if the command exits non-zero, if `olddefconfig` drops the
new symbols, or if ABI/KMI checks fail.

Check the result:

```bash
test -s "$OUT_DIR/msm-kernel/.config"
test -s "$OUT_DIR/dist/vmlinux"
test -s "$OUT_DIR/dist/boot.img"

grep -E 'CONFIG_(BPF_SYSCALL|BPF_JIT|DEBUG_INFO_BTF|DEBUG_INFO_BTF_MODULES|SCHED_CLASS_EXT|EXT_GROUP_SCHED|SCHED_PROXY_EXEC)' \
  "$OUT_DIR/msm-kernel/.config"

find "$OUT_DIR/dist" -maxdepth 1 -type f -printf '%f\n' | sort
```

### 6.1. KMI and vendor-module gate

The `gen3egg` GKI build uses strict Android KMI/ABI settings and builds
`vendor_dlkm.img`. A scheduler backport can change exported symbols, BTF, and
module interfaces. Do not bypass an ABI failure by deleting checks or by
reusing old Qualcomm modules.

Before booting, verify:

```bash
find "$OUT_DIR/dist" -maxdepth 1 -type f \
  \( -name '*.ko' -o -name 'modules.*' -o -name 'vendor_dlkm.img' \) \
  -print | sort

dmesg 2>/dev/null | grep -iE 'module|kmi|abi|btf|sched_ext' || true
```

All loadable modules that depend on changed kernel interfaces must be rebuilt
from the same source/configuration. A kernel image built with new scheduler
interfaces and old vendor `.ko` files is not a valid test.

## 7. Build `scx` for the target ABI

The current `scx` workspace already documents x86_64 and aarch64 builds in
`scheds/rust/scx_fcp/SETUP.md`. Rebuild after the kernel backport; do not
assume an existing 6.18 bundle is compatible with a 6.1 backport.

### 7.1. `scx`-only rebuild when the 6.1 kernel is already built

This section does **not** rebuild `boot.img`, `vendor_boot.img`, or any kernel
module. It only rebuilds the user-space schedulers against the current
`scx` checkout. The target kernel must already boot with
`CONFIG_SCHED_CLASS_EXT=y`, BTF, and the backported kfunc/struct-ops ABI.

For an ARM64 Android target:

```bash
cd /home/worker/workspace/scx
export PATH="/home/worker/.cargo/bin:$PATH"
CARGO_TARGET_AARCH64_UNKNOWN_LINUX_GNU_LINKER=aarch64-linux-gnu-gcc \
RUSTFLAGS="-C target-feature=+crt-static" \
cargo build --release --target aarch64-unknown-linux-gnu \
  -p scx_p2dq_k61 -p scx_lavd_k61 -p scx_fcp

file target/aarch64-unknown-linux-gnu/release/scx_fcp \
     target/aarch64-unknown-linux-gnu/release/scx_lavd_k61 \
     target/aarch64-unknown-linux-gnu/release/scx_p2dq_k61
```

The `scx_lavd_k61` and `scx_p2dq_k61` binaries are useful for confirming that the
cross-build works, but they cannot load on Linux 6.1 for the reasons in
section 5.4. Only `scx_fcp` is a supported attach target here. A successful
Rust link does not prove that the target kernel accepts the generated BPF
struct-ops and BTF relocations.

Push the static binaries only after the architecture check:

```bash
adb -s 88249622 push target/aarch64-unknown-linux-gnu/release/scx_p2dq_k61 \
  /data/local/tmp/
adb -s 88249622 push target/aarch64-unknown-linux-gnu/release/scx_lavd_k61 \
  /data/local/tmp/
adb -s 88249622 push target/aarch64-unknown-linux-gnu/release/scx_fcp \
  /data/local/tmp/
adb -s 88249622 shell chmod 755 \
  /data/local/tmp/scx_p2dq_k61 /data/local/tmp/scx_lavd_k61 \
  /data/local/tmp/scx_fcp
```

Verify the kernel and attachment separately:

```bash
adb shell 'uname -a; cat /sys/kernel/sched_ext/state; \
  cat /sys/kernel/sched_ext/root/ops; tail -n 30 /data/local/tmp/scx_fcp.log'
```

Do not label the run as a 6.1-backport result unless `uname -a` identifies the
6.1 kernel and `root/ops` reports `scx_fcp` without BPF/BTF/verifier errors.

```bash
cd /home/worker/workspace/scx

cargo build --release -p scx_fcp -p scx_lavd_k61 -p scx_p2dq_k61
make -C tools/scx_fcp_bench

CARGO_TARGET_AARCH64_UNKNOWN_LINUX_GNU_LINKER=aarch64-linux-gnu-gcc \
  cargo build --release \
    --target aarch64-unknown-linux-gnu \
    -p scx_fcp -p scx_lavd_k61 -p scx_p2dq_k61

aarch64-linux-gnu-gcc -O2 -pthread \
  tools/scx_fcp_bench/fcp_frame_bench.c \
  -o tools/scx_fcp_bench/fcp_frame_bench_aarch64 \
  -lm
```

The user-space loader, libbpf, BTF, and kernel `sched_ext` ABI must agree. If
the loader reports an unknown command, missing kfunc, invalid struct-op, or
BTF relocation failure, stop and resolve the ABI mismatch; do not suppress the
error.

Recorded build status:

- The initial ARM64 `scx_fcp` build failed at link time because the cross
  sysroot lacked `libelf` and `zlib` (`-lelf` and `-lz`).
- A fresh ARM64 release build of all three schedulers completed successfully:
  `scx_fcp`, `scx_lavd_k61`, and `scx_p2dq_k61`.
- The fresh binaries are in
  `target/aarch64-unknown-linux-gnu/release/` and are ELF AArch64 binaries.
- The current tree does not contain `fcp_frame_bench_aarch64`.
- The x86_64 `scx_fcp` build completed and was pushed, but loading it failed
  with a BPF verifier `-EINVAL` in `fcp_select_cpu`; this is not a successful
  scheduler attach.

For a physical ARM64 device, package the aarch64 binaries with their loader
and shared libraries as described in `SETUP.md`. Confirm each binary with:

```bash
file target/aarch64-unknown-linux-gnu/release/scx_fcp
file target/aarch64-unknown-linux-gnu/release/scx_lavd_k61
file target/aarch64-unknown-linux-gnu/release/scx_p2dq_k61
file tools/scx_fcp_bench/fcp_frame_bench_aarch64
```

## 8. Boot safely

### 8.1. Preferred order

1. Boot the new kernel in an emulator or disposable target.
2. Verify the kernel version and `sched_ext` sysfs interface.
3. Load `scx_fcp` and run a short synthetic benchmark.
4. Confirm that vendor modules load without vermagic/KMI/BTF errors.
5. Only then test on the physical EGG device.

### 8.2. One-time boot test

Use the device's documented bootloader procedure and inspect its partition
layout first:

```bash
adb reboot bootloader
fastboot getvar product 2>&1 | tee /tmp/fastboot-product.txt
fastboot getvar current-slot 2>&1 | tee /tmp/fastboot-slot.txt
fastboot getvar all 2>&1 | tee /tmp/fastboot-vars.txt
```

If the bootloader supports a non-persistent RAM boot and the generated
`boot.img` is self-contained for the test, use:

```bash
fastboot boot "$OUT_DIR/dist/boot.img"
```

Do not assume that `fastboot boot` is sufficient when new modules are needed
from `vendor_dlkm.img`, `system_dlkm.img`, or `vendor_boot.img`. In that case,
use the device-specific packaging and flashing procedure and keep the original
slot untouched until the one-time boot path is validated.

Do not flash `boot_a`, `boot_b`, `vendor_dlkm_a`, or similar partition names
without confirming that those partitions exist on the target. The names vary
between products.

## 9. Verify the running kernel

After Android reaches the shell:

```bash
adb shell 'uname -a'
adb shell 'cat /sys/kernel/sched_ext/state'
adb shell 'cat /sys/kernel/sched_ext/root/ops'
adb shell 'dmesg | grep -iE "sched_ext|bpf|btf|module|kmi|abi" | tail -n 100'
```

The first gate is that `/sys/kernel/sched_ext/state` exists. The scheduler is
not ready to run if the file is absent, even if a `sched_ext` string appears in
the build log.

## 10. Run `scx_fcp` and the benchmark

Push the rebuilt static binary:

```bash
adb -s 88249622 push \
  target/aarch64-unknown-linux-gnu/release/scx_fcp /data/local/tmp/
```

Start the scheduler only on a rooted/userdebug test image. `setenforce 0` is
an engineering-test workaround, not a production configuration:

```bash
adb -s 88249622 shell 'su 0 sh -c "
  chmod 755 /data/local/tmp/scx_fcp
  ulimit -l unlimited
  nohup /data/local/tmp/scx_fcp -f 16667 -s 2 \
    > /data/local/tmp/scx_fcp.log 2>&1 < /dev/null &
"'
```

Verify attachment before running a longer experiment:

```bash
adb -s 88249622 shell 'cat /sys/kernel/sched_ext/state; \
  cat /sys/kernel/sched_ext/root/ops; \
  tail -n 20 /data/local/tmp/scx_fcp.log'
```

Run a short synthetic workload:

```bash
adb shell '/data/local/tmp/scx/lib/ld-linux-aarch64.so.1 \
  --library-path /data/local/tmp/scx/lib \
  /data/local/tmp/scx/fcp_frame_bench \
  --scheduler scx_fcp \
  --frames 300 \
  --hz 60 \
  --stages 4 \
  --stage-us 1200 \
  --bg-workers 16'
```

Before switching to another scheduler, terminate the specific scheduler PID
and verify that `root/ops` is no longer the old scheduler:

```bash
SCX_PID="$(adb shell pidof scx_fcp | tr -d '\r')"
test -n "$SCX_PID" && adb shell "su 0 kill $SCX_PID"
adb shell 'cat /sys/kernel/sched_ext/state; cat /sys/kernel/sched_ext/root/ops'
```

Do not attempt to attach `scx_lavd_k61` or `scx_p2dq_k61` on Linux 6.1; their
unsupported arena and iterator features make those loads fail by design.

## 11. Failure interpretation

| Symptom | Likely boundary |
|---|---|
| `CONFIG_SCHED_CLASS_EXT` is unknown | Backport/Kconfig series is incomplete |
| Build fails in scheduler core | Semantic conflict with Android 6.1 or vendor scheduler changes |
| BTF generation fails | `pahole`, DWARF, LLVM, or BTF configuration problem |
| ABI/KMI check fails | New interfaces require review; do not bypass the check |
| `sched_ext/state` is absent after boot | The booted image is not the rebuilt kernel, or the feature is not built in |
| BPF struct-op load fails | Kernel/user-space `sched_ext`, BTF, kfunc, or libbpf mismatch |
| `Exec format error`/invalid module | Wrong architecture, vermagic, KMI, or stale vendor module |
| Android boots but vendor services fail | Required vendor modules/images were not rebuilt or packaged together |
| Attach fails under SELinux or memlock limits | Test-image policy/limits; fix explicitly and record the workaround |

## 12. Completion checklist

Do not describe the Android 6.1 backport as complete until all items below
have evidence attached to the run:

- [ ] Exact `msm-kernel` source commit and backport patch range recorded.
- [ ] Existing `common` changes preserved and repository ownership documented.
- [ ] `CONFIG_SCHED_CLASS_EXT` and dependencies survive `olddefconfig`.
- [ ] `build/build.sh` completes successfully for `gen3egg` GKI.
- [ ] ABI/KMI checks pass or have an explicit reviewed update.
- [ ] `boot.img` and every required DLKM/module image come from the same build.
- [ ] Kernel boots and reports the expected version.
- [ ] `/sys/kernel/sched_ext/state` and `root/ops` are present.
- [ ] `scx_fcp` attaches without BPF/BTF/kfunc errors.
- [ ] `fcp_frame_bench` completes and its raw output is saved.
- [ ] No vendor-module, vermagic, KMI, or early-boot errors occur.
- [ ] Physical-device flashing, if performed, is recorded separately from
      emulator/one-time-boot validation.

The checklist remains **not complete**. Ada history verifies that the
`common`-tree backport was committed and that Android product `boot.img`
files were produced, but it does not verify that those images came from the
same source/module set now on disk, that the kernel booted with
`sched_ext`, or that any `scx` scheduler attached and completed the benchmark.

## 13. Restore the original `msm-kernel` checkout

After preserving the patch series, build logs, images, and test output, return
to the exact pre-backport commit. Do this only when the branch has no unresolved
working-tree changes:

```bash
cd "$KP"
git -C msm-kernel status --short
git -C msm-kernel switch --detach "$BASE_MSM_COMMIT"
```

Keep `backport/sched-ext-6.1` for review and reproducibility. If
`BASE_MSM_REF` was non-empty and the original branch is still available, it can
be checked out explicitly instead of using detached HEAD:

```bash
test -n "$BASE_MSM_REF" && git -C msm-kernel switch "$BASE_MSM_REF"
```
