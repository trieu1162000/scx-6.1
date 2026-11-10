#!/bin/bash
# Build the tuned scx_fcp profiles and synthetic benchmark for Linux 6.1.
#
# Usage:
#   scripts/build-6.1-fcp.sh
#   TARGET=aarch64-unknown-linux-gnu CC=aarch64-linux-gnu-gcc \
#     scripts/build-6.1-fcp.sh
#   BUILD_BASELINES=1 scripts/build-6.1-fcp.sh
#
# Outputs, by default under build/6.1-fcp/bin:
#   scx_fcp                default tuned profile
#   scx_fcp_named          named-root profile
#   scx_fcp_behavioral     zero-hint profile
#   fcp_frame_bench        synthetic benchmark
#   scx_lavd, scx_p2dq     optional 6.1 baselines with BUILD_BASELINES=1
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=${OUT:-$ROOT/build/6.1-fcp/bin}
LOG_DIR=${LOG_DIR:-$ROOT/build/6.1-fcp/logs}
TARGET=${TARGET:-}
JOBS=${JOBS:-}
CC=${CC:-cc}

CARGO_ARGS=(--release -p scx_fcp)
TARGET_DIR=$ROOT/target
if [ -n "$TARGET" ]; then
    CARGO_ARGS+=(--target "$TARGET")
    TARGET_DIR=$TARGET_DIR/$TARGET
fi
if [ -n "$JOBS" ]; then
    CARGO_ARGS+=(-j "$JOBS")
fi
TARGET_DIR=$TARGET_DIR/release

mkdir -p "$OUT" "$LOG_DIR"

build_profile() {
    local profile=$1 name=$2 log=$LOG_DIR/build-$2.log
    echo "build $name${TARGET:+ for $TARGET}"
    (
        cd "$ROOT"
        SCX_FCP_BUILD=$profile cargo build "${CARGO_ARGS[@]}"
    ) >"$log" 2>&1 || {
        echo "build failed; see $log" >&2
        exit 1
    }
    install -m 755 "$TARGET_DIR/scx_fcp" "$OUT/$name"
}

build_profile "" scx_fcp
build_profile named scx_fcp_named
build_profile zero-hint scx_fcp_behavioral

if [ "${BUILD_BASELINES:-0}" = 1 ]; then
    echo "build 6.1 baselines"
    (
        cd "$ROOT"
        cargo build --release -p scx_lavd_k61 -p scx_p2dq_k61 "${CARGO_ARGS[@]:2}"
    ) >"$LOG_DIR/build-baselines.log" 2>&1 || {
        echo "baseline build failed; see $LOG_DIR/build-baselines.log" >&2
        exit 1
    }
    install -m 755 "$TARGET_DIR/scx_lavd_k61" "$OUT/scx_lavd"
    install -m 755 "$TARGET_DIR/scx_p2dq_k61" "$OUT/scx_p2dq"
fi

echo "build fcp_frame_bench"
make -C "$ROOT/scheds/rust/scx_fcp/tools/scx_fcp_bench" \
    CC="$CC" clean all >"$LOG_DIR/build-bench.log" 2>&1 || {
    echo "benchmark build failed; see $LOG_DIR/build-bench.log" >&2
    exit 1
}
install -m 755 "$ROOT/scheds/rust/scx_fcp/tools/scx_fcp_bench/fcp_frame_bench" \
    "$OUT/fcp_frame_bench"

(
    cd "$OUT"
    sha256sum scx_fcp scx_fcp_named scx_fcp_behavioral fcp_frame_bench \
        >"$OUT/SHA256SUMS"
    if [ "${BUILD_BASELINES:-0}" = 1 ]; then
        sha256sum scx_lavd scx_p2dq >>"$OUT/SHA256SUMS"
    fi
)

echo "outputs in $OUT"
