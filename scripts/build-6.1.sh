#!/bin/bash
# Build the 6.1 schedulers.
#
# Usage:
#   scripts/build-6.1.sh              # native build
#   scripts/build-6.1.sh --target aarch64-unknown-linux-gnu
#
# For the cross build, export the usual CARGO_TARGET_AARCH64_* linker and
# RUSTFLAGS (see README.md).
#
# The BPF objects compile against the bundled upstream vmlinux.h for type
# definitions; the Linux 6.1 ABI values come from the enum tables generated
# from scheds/include/vmlinux/vmlinux-6.1.h. See README.md.
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
exec cargo build --release \
    -p scx_lavd_k61 -p scx_p2dq_k61 -p scx_fcp "$@"
