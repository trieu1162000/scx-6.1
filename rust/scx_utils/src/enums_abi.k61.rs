// Linux 6.1 sched_ext backport enumerator overrides.
//
// The 6.1 backport used by this tree (include/linux/sched/ext.h) differs from
// the bundled upstream vmlinux.h in one 64-bit enumerator. The ABI selection
// is per process and opt-in: only binaries that call
// `scx_utils::compat::set_enum_abi(EnumAbi::Linux61)` consult this table.
//
// SCX_TASK_CURSOR also differs in the source (1U << 31 versus a signed
// -2147483648), but scx_ent_flags is a 4-byte enum, so the truncated-enum
// recovery path is never entered for it. Only values above UINT32_MAX can be
// affected by BTF_KIND_ENUM64 truncation; values that fit in 32 bits always
// come from the running kernel's BTF.

pub const ENUM_ABI_OVERRIDES_K61: &[(&str, &str, u64)] = &[
    // 6.1 backport: 1ULL << 36. Bundled upstream vmlinux.h: 1ULL << 42.
    ("scx_enq_flags", "SCX_ENQ_BLOCKED", 0x1000000000),
];
