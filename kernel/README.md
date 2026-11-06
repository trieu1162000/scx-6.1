# Kernel-side backport

`0001-scx-6.1-minimal-backport.patch` adds `sched_ext` to the Android 14
Linux 6.1 kernel.

## Apply

```sh
cd <kernel-tree>
git am /path/to/kernel/0001-scx-6.1-minimal-backport.patch
```

The patch applies to the tree that produces the running kernel image. On
Qualcomm `kernel_platform` devices, verify which tree is built before
enabling configs:

```sh
grep -l bpf_scx_reg \
  kernel_platform/msm-kernel/kernel/sched/ext.c \
  kernel_platform/common/kernel/sched/ext.c
```

## What it contains

- sched_ext core (`kernel/sched/ext.c`, `include/linux/sched/ext.h`) and
  the CFS hooks in `kernel/sched/fair.c`.
- `CONFIG_SCHED_CLASS_EXT` (depends on `BPF_SYSCALL && BPF_JIT &&
  DEBUG_INFO_BTF`).
- `BPF_COMPLEXITY_LIMIT_INSNS` 1M -> 4M. The merged-TU `scx_lavd` object
  measured 2,030,992 processed instructions under the 6.1 verifier; 4M
  leaves a 1.97x margin. `MAX_CALL_FRAMES` is left at 8 (measured maximum
  call depth: 5).
- BTF kptr fixes: the `off % align` check now runs only for members that
  match the field type, and unknown type tags are ignored instead of
  failing the whole map. Without this, maps with nested structs lose their
  kptr table silently.
- kfunc registration is independent per program type, so one failing type
  does not drop the `SYSCALL` kfunc set.
- `struct bpf_cpumask *` is accepted where a `cpumask` argument is
  expected, matching the upstream `KF_ARG_PTR_TO_CPUMASK` behaviour.
- `bpf_scx_reg` keeps the name probed by `scx_utils`.

## Config

```
CONFIG_BPF=y
CONFIG_BPF_SYSCALL=y
CONFIG_BPF_JIT=y
CONFIG_BPF_JIT_ALWAYS_ON=y
CONFIG_BPF_JIT_DEFAULT_ON=y
CONFIG_DEBUG_INFO_BTF=y
CONFIG_SCHED_CLASS_EXT=y
CONFIG_KALLSYMS_ALL=y
CONFIG_FUNCTION_TRACER=y
CONFIG_FTRACE_SYSCALLS=y
CONFIG_IKCONFIG_PROC=y
```

`CONFIG_DYNAMIC_FTRACE` follows `CONFIG_FUNCTION_TRACER` automatically.

### GKI note

If the kernel image is built from the AOSP `common` tree, add the ftrace
options to `common/arch/arm64/configs/gki_defconfig`. If it is built from
`msm-kernel`, use the vendor fragment merged last (for example
`msm-kernel/arch/arm64/configs/vendor/gen3egg_GKI.config`).

`gki_defconfig` is checked against `savedefconfig` output. When editing it,
place `CONFIG_FUNCTION_TRACER=y` and `CONFIG_FTRACE_SYSCALLS=y` in the
position `savedefconfig` expects, and do not add `CONFIG_DYNAMIC_FTRACE=y`
(it is a default-y symbol and would make the check fail).

## pahole

`CONFIG_DEBUG_INFO_BTF` needs pahole at build time. pahole >= 1.24 emits
`BTF_KIND_ENUM64`; pahole 1.23 truncates 64-bit enumerators to their low 32
bits. The scx side recovers the values, so 1.23 still works, but upgrading
the prebuilt pahole removes the warning and the recovery path entirely.

## Measurement caveat

The backport hooks the CFS scheduler path rather than adding a dedicated
`ext_sched_class`. Scheduling results are therefore not directly comparable
with upstream sched_ext kernels (6.12+).
