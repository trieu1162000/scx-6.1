#!/bin/bash
# Generate scheds/include/vmlinux/vmlinux-6.1-<arch>.h from a 6.1 kernel.
#
# Usage:
#   scripts/gen-vmlinux-6.1.sh <vmlinux-or-btf> <arch>
#
# Examples:
#   scripts/gen-vmlinux-6.1.sh /path/to/vmlinux x86_64
#   scripts/gen-vmlinux-6.1.sh /tmp/vmlinux-6.1.btf arm64
#
# The input can be an ELF vmlinux (with .BTF) or a raw BTF blob.
set -eu

if [ $# -ne 2 ]; then
    echo "usage: $0 <vmlinux-or-btf> <arch>" >&2
    exit 1
fi

SRC=$1
ARCH=$2
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT="$ROOT/scheds/include/vmlinux/vmlinux-6.1-$ARCH.h"

command -v bpftool >/dev/null || {
    echo "bpftool is required" >&2
    exit 1
}

bpftool btf dump file "$SRC" format c > "$OUT"
ln -sf "vmlinux-6.1-$ARCH.h" "$ROOT/scheds/include/vmlinux/vmlinux.h"

# Kernels built with pahole < 1.24 lack BTF_KIND_ENUM64 and truncate 64-bit
# enumerators to their low 32 bits. Repair the known sched_ext enums from the
# 6.1 source (include/linux/sched/ext.h).
if grep -q 'SCX_ENQ_BLOCKED = 0ULL' "$OUT" 2>/dev/null; then
    echo "warning: BTF lacks BTF_KIND_ENUM64; repairing sched_ext enums" >&2
    python3 - "$OUT" <<'PY'
import re
import sys

path = sys.argv[1]
text = open(path).read()

fixes = {
    "scx_dsq_id_flags": {
        "SCX_DSQ_FLAG_BUILTIN": "9223372036854775808ULL",
        "SCX_DSQ_FLAG_LOCAL_ON": "4611686018427387904ULL",
        "SCX_DSQ_INVALID": "9223372036854775808ULL",
        "SCX_DSQ_GLOBAL": "9223372036854775809ULL",
        "SCX_DSQ_LOCAL": "9223372036854775810ULL",
        "SCX_DSQ_BYPASS": "9223372036854775811ULL",
        "SCX_DSQ_REJECT": "9223372036854775812ULL",
        "SCX_DSQ_RESCUE": "9223372036854775813ULL",
        "SCX_DSQ_LOCAL_ON": "13835058055282163712ULL",
        "SCX_DSQ_LOCAL_CPU_MASK": "4294967295ULL",
    },
    "scx_enq_flags": {
        "SCX_ENQ_HEAD": "16ULL",
        "SCX_ENQ_CPU_SELECTED": "1024ULL",
        "SCX_ENQ_PREEMPT": "4294967296ULL",
        "SCX_ENQ_IMMED": "8589934592ULL",
        "SCX_ENQ_RESCUE": "17179869184ULL",
        "SCX_ENQ_PREEMPT_LAZY": "34359738368ULL",
        "SCX_ENQ_BLOCKED": "68719476736ULL",
        "SCX_ENQ_REENQ": "1099511627776ULL",
        "SCX_ENQ_LAST": "2199023255552ULL",
        "SCX_ENQ_CLEAR_OPSS": "72057594037927936ULL",
        "SCX_ENQ_DSQ_PRIQ": "144115188075855872ULL",
    },
    "scx_deq_flags": {
        "SCX_DEQ_CORE_SCHED_EXEC": "4294967296ULL",
        "SCX_DEQ_SCHED_CHANGE": "8589934592ULL",
    },
    "scx_public_consts": {
        "SCX_SLICE_INF": "18446744073709551615ULL",
    },
}

for etype, values in fixes.items():
    m = re.search(r"enum %s \{(.*?)\};" % re.escape(etype), text, re.S)
    if not m:
        continue
    body = m.group(1)
    for name, val in values.items():
        body = re.sub(r"(\b%s\s*=\s*)[^,\n]+" % re.escape(name),
                      r"\g<1>%s" % val, body)
    text = text[:m.start(1)] + body + text[m.end(1):]

open(path, "w").write(text)
PY
fi

echo "generated $OUT"
echo "regenerate the enum tables with:"
echo "  python3 scripts/gen_enum_defs.py \\"
echo "    scheds/include/vmlinux/vmlinux-6.1-$ARCH.h \\"
echo "    scheds/k6.1/include/scx/enum_defs.autogen.h \\"
echo "    scheds/k6.1/include/scx/enums_abi.autogen.h \\"
echo "    rust/scx_utils/src/enums_abi.autogen.rs"
