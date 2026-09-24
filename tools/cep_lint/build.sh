#!/bin/sh
# CEP:FILE: tools/cep_lint/build.sh
# CEP:WHAT: Build script for cep_lint with the standard's minimum warning set (CEP&CC 6.3) and optional sanitizer mode.
# CEP:WHY: Warnings are errors; sanitizer builds are required in CI (CEP&CC 6.6).
# CEP:CLASS: CEP-2
# CEP:STATUS: complete
# CEP:FAILURE: Exits non-zero on any compile or link failure.
# CEP:ASSUMES: CXX defaults to g++ with C++26 support; MODE=asan enables AddressSanitizer and UndefinedBehaviorSanitizer.
# CEP:COST: Offline tool; full rebuild each run.
# CEP:EVIDENCE: .github/workflows/cep_lint.yml.

set -e

CXX="${CXX:-g++}"
STD="${STD:-c++26}"
MODE="${MODE:-release}"
SRC_DIR="$(dirname "$0")"
OUT_DIR="${SRC_DIR}/build"
OUT="${OUT_DIR}/cep_lint"

WARNINGS="-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Woverloaded-virtual -Wformat=2 -Werror"

mkdir -p "${OUT_DIR}"

SOURCES="${SRC_DIR}/src/json_model.cpp ${SRC_DIR}/src/json_parse.cpp ${SRC_DIR}/src/diagnostics.cpp ${SRC_DIR}/src/scanner.cpp ${SRC_DIR}/src/config.cpp ${SRC_DIR}/src/checks.cpp ${SRC_DIR}/src/linter.cpp ${SRC_DIR}/src/reporter.cpp ${SRC_DIR}/src/cli.cpp ${SRC_DIR}/src/selftest.cpp ${SRC_DIR}/src/main.cpp"

if [ "${MODE}" = "asan" ]; then
    # CEP:WHAT: GCC 14 sanitizer build workaround.
    # CEP:WHY: GCC 14 emits a known false-positive maybe-uninitialized diagnostic from libstdc++'s std::regex internals under -fsanitize=address (CEP&CC 8.4 documented compiler workaround).
    # CEP:STATUS: complete
    # CEP:FAILURE: None; release builds keep the full warning set.
    SANITIZERS="-fsanitize=address,undefined -fno-omit-frame-pointer -Wno-maybe-uninitialized"
else
    SANITIZERS=""
fi

# shellcheck disable=SC2086
"${CXX}" -std="${STD}" ${WARNINGS} ${SANITIZERS} -O2 ${SOURCES} -o "${OUT}"
echo "built ${OUT}"
