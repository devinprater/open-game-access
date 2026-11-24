#!/usr/bin/env bash
# n64-android-cmake-test.sh — build the Android Nintendo 64 subproject that
# scripts/n64-android-cmake.py writes (the overlay's section 10c), on the
# host, with CMake and clang, and boot it with the host proof's driver.
#
# WHY. The Android APK build only proves the subproject COMPILES and LINKS
# for the phone; nothing boots there in CI. This builds the very same
# generated CMakeLists.txt, staged in the very same layout the overlay
# stages it in (src/, glue/, gen/), so its CMake wiring is exercised end to
# end: the OBJECT library, the numbered copies and the rename
# (scripts/m64p-archive.sh running scripts/m64p-isolate.sh), the archive
# carried behind the glue as an INTERFACE dependency — and then the result
# boots a synthetic cartridge (scripts/n64-smoke-rom.py) through
# scripts/n64-host-proof-main.cpp, the driver scripts/n64-host-proof.sh
# uses: video, audio, RDRAM reads, savestates and a second core.
#
# Usage: n64-android-cmake-test.sh   (M64P_SRC as scripts/core-sources.sh
#                                     reads it; fetched at the pin when absent)
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$ROOT/scripts/core-sources.sh"

for tool in cmake python3; do
  command -v "$tool" >/dev/null || { echo "!! $tool is not installed" >&2; exit 1; }
done
GEN=()
command -v ninja >/dev/null && GEN=(-G Ninja)
CC="${CC:-$(command -v clang || command -v gcc)}"
CXX="${CXX:-$(command -v clang++ || command -v g++)}"

if [ ! -d "$M64P_SRC/mupen64plus-core" ]; then
  rev="$(grep -o 'M64P_REV:-[0-9a-f]\{40\}' "$ROOT/scripts/bootstrap-deps.sh" | head -1 | cut -d- -f2)"
  repo="$(grep -o 'M64P_REPO:-[^}]*' "$ROOT/scripts/bootstrap-deps.sh" | head -1 | cut -d- -f2-)"
  [ -n "$rev" ] && [ -n "$repo" ] || { echo "!! no M64P pin in bootstrap-deps.sh" >&2; exit 1; }
  echo "== fetching Mupen64Plus-Next $rev into $M64P_SRC"
  mkdir -p "$M64P_SRC" && git -C "$M64P_SRC" init -q && \
    git -C "$M64P_SRC" fetch -q --depth 1 "$repo" "$rev" && \
    git -C "$M64P_SRC" checkout -q FETCH_HEAD || { echo "!! fetch failed" >&2; exit 1; }
fi

OUT="${N64_CMAKE_OUT:-$ROOT/Vendor/n64cmake}"
N64_DIR="$OUT/n64"; TOP="$OUT/top"; BUILD="$OUT/build"
mkdir -p "$OUT"

# ---- stage exactly as android-apply-overlay.sh section 10c does ----
rm -rf "$N64_DIR/glue" "$N64_DIR/gen" "$TOP"
mkdir -p "$N64_DIR" "$N64_DIR/glue" "$N64_DIR/gen" "$TOP"
if [ ! -f "$N64_DIR/src/.oga-staged" ] || [ "$(cat "$N64_DIR/src/.oga-staged")" != "$M64P_SRC" ]; then
  rm -rf "$N64_DIR/src"
  cp -r "$M64P_SRC" "$N64_DIR/src" && rm -rf "$N64_DIR/src/.git" || exit 1
  echo "$M64P_SRC" > "$N64_DIR/src/.oga-staged"
fi
cp "$ROOT/Core/n64_core.cpp" "$ROOT/Core/n64_m64p_stubs.c" "$N64_DIR/glue/"
cp "$ROOT"/Core/*.h "$N64_DIR/glue/"
cp -r "$ROOT/Core/n64" "$N64_DIR/glue/n64"
cp "$ROOT/scripts/m64p-isolate.sh" "$ROOT/scripts/m64p-archive.sh" "$N64_DIR/gen/"
printf '// empty\n' > "$N64_DIR/gen/oga_n64_empty.cpp"
python3 "$ROOT/scripts/n64-android-cmake.py" "$ROOT/scripts/core-sources.sh" \
  "$N64_DIR/CMakeLists.txt" "$N64_DIR" || exit 1

# The frontend's role, played by a driver executable: it links ONLY the glue,
# so the core can reach it solely through the INTERFACE dependency.
cat > "$TOP/CMakeLists.txt" <<'EOF'
cmake_minimum_required(VERSION 3.22)
project(oga-n64-host C CXX)
add_subdirectory(${N64DIR} ./oga-n64)
add_executable(n64-cmake-proof ${PROOF_MAIN})
set_target_properties(n64-cmake-proof PROPERTIES CXX_STANDARD 17)
target_include_directories(n64-cmake-proof PRIVATE ${N64DIR}/glue)
target_link_libraries(n64-cmake-proof oga-n64-glue pthread m)
target_link_options(n64-cmake-proof PRIVATE -Wl,--no-undefined)
EOF

cmake -S "$TOP" -B "$BUILD" "${GEN[@]}" -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER="$CC" -DCMAKE_CXX_COMPILER="$CXX" \
  -DN64DIR="$N64_DIR" -DPROOF_MAIN="$ROOT/scripts/n64-host-proof-main.cpp" \
  > "$OUT/cfg.log" 2>&1 || { tail -30 "$OUT/cfg.log" >&2; echo "!! cmake configure failed" >&2; exit 1; }
cmake --build "$BUILD" -j "${JOBS:-$(nproc 2>/dev/null || echo 4)}" > "$OUT/build.log" 2>&1 \
  || { grep -n "error\|!!" "$OUT/build.log" | head -30 >&2; echo "!! the generated subproject did not build" >&2; exit 1; }
grep -q "m64p-archive: .* objects renamed" "$OUT/build.log" \
  || { echo "!! the build never ran the rename pass (m64p-archive.sh)" >&2; exit 1; }
echo "ok: the generated subproject builds; $(grep -o 'm64p-archive: [0-9]* objects renamed' "$OUT/build.log" | head -1)"

# ---- every core global in the archive carries the prefix ----
# (A name that already starts with an underscore keeps it in front of the
# prefix: m64p-isolate.sh's Mach-O spelling rule, harmless on ELF.)
NM="$(command -v llvm-nm || command -v nm)"
leaked="$("$NM" -g --defined-only "$BUILD/oga-n64/liboga-m64p.a" 2>/dev/null \
  | awk 'NF==3 && $2 ~ /[A-Z]/ && $2 != "W" && $2 != "V" {print $3}' \
  | grep -v '^_\{0,1\}oga_m64p_' | head -5)"
if [ -n "$leaked" ]; then
  echo "FAIL: unprefixed globals in the archive the frontend links:" >&2
  echo "$leaked" >&2
  exit 1
fi
echo "ok: every strong global in liboga-m64p.a carries the oga_m64p_ prefix"

# ---- boot it ----
python3 "$ROOT/scripts/n64-smoke-rom.py" "$OUT/smoke.z64" --byteorder z64 || exit 1
python3 "$ROOT/scripts/n64-smoke-rom.py" "$OUT/smoke.v64" --byteorder v64 || exit 1
RUN="$OUT/run"; rm -rf "$RUN"; mkdir -p "$RUN"
( cd "$RUN" && timeout 300 "$BUILD/n64-cmake-proof" "$OUT/smoke.z64" "$OUT/smoke.v64" "$RUN/smoke.srm" "$RUN/smoke.state" ) 2>&1 \
  | tee "$OUT/proof.log"
rc=${PIPESTATUS[0]}
[ "$rc" -eq 0 ] || { echo "!! the Android subproject's build did not boot (exit $rc)" >&2; exit 1; }
echo "PASS: the Android Nintendo 64 subproject builds, isolates and boots on the host"
