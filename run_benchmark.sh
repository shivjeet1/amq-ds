#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────────────────
# run_benchmark.sh
# Build and run the AMQ benchmark, optionally piping /usr/share/dict/words.
# ─────────────────────────────────────────────────────────────────────────────
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"

echo "═══════════════════════════════════════════════════════════════"
echo "  AMQ Benchmark — Build & Run Script"
echo "═══════════════════════════════════════════════════════════════"

# ── Build ────────────────────────────────────────────────────────────────────
echo ""
echo "[1/3] Configuring CMake (Release mode)..."
cmake -S "${SCRIPT_DIR}" -B "${BUILD_DIR}" \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
      2>&1 | grep -v "^--"

echo ""
echo "[2/3] Compiling..."
cmake --build "${BUILD_DIR}" --parallel "$(nproc)"

BINARY="${BUILD_DIR}/amq_benchmark"

# ── Run ──────────────────────────────────────────────────────────────────────
echo ""
echo "[3/3] Running benchmark..."

DICT="/usr/share/dict/words"
if [[ -f "${DICT}" ]]; then
    echo "      Using dictionary: ${DICT}"
    "${BINARY}" "${DICT}"
else
    echo "      Dictionary not found — using synthetic random strings."
    "${BINARY}"
fi

echo ""
echo "Done. Benchmark output shown above."
