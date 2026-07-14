#!/usr/bin/env bash
#
# Standalone native build of the whd_gen WAD->WHD/WHX conversion tool.
#
# whd_gen is a host-only tool and needs no SDL / pico-sdk. The top-level
# CMake project can also build it (cmake .. && make whd_gen), but that
# requires SDL2/SDL2_mixer/SDL2_net dev packages for the rest of the tree
# even though whd_gen itself does not. This script builds just whd_gen with
# nothing but a C/C++ toolchain.
#
# Usage:
#   ./build_native.sh            # builds ./whd_gen
#   CXX=clang++ CC=clang ./build_native.sh
#
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"   # src/whd_gen
src="$(cd "$here/.." && pwd)"                           # src

CC="${CC:-cc}"
CXX="${CXX:-c++}"
CFLAGS="${CFLAGS:--O2}"
CXXFLAGS="${CXXFLAGS:--O2}"

inc=(-I"$src" -I"$src/doom" -I"$here" -I"$src/adpcm-xq")
def=(-DIS_WHD_GEN=1)

obj_dir="$(mktemp -d)"
trap 'rm -rf "$obj_dir"' EXIT

objs=()

# C sources (must be compiled as C, not C++).
for c in \
    "$src/tiny_huff.c" \
    "$src/musx_decoder.c" \
    "$src/image_decoder.c" \
    "$src/adpcm-xq/adpcm-lib.c"; do
    o="$obj_dir/$(basename "${c%.c}").o"
    echo "CC  $c"
    "$CC" -std=c11 $CFLAGS "${def[@]}" "${inc[@]}" -c "$c" -o "$o"
    objs+=("$o")
done

# C++ sources.
for cc in \
    "$here/whd_gen.cpp" \
    "$here/mus2seq.cpp" \
    "$here/huff.cpp" \
    "$here/lodepng.cpp" \
    "$here/compress_mus.cpp" \
    "$here/wad.cpp"; do
    o="$obj_dir/$(basename "${cc%.cpp}").o"
    echo "CXX $cc"
    "$CXX" -std=c++17 $CXXFLAGS "${def[@]}" "${inc[@]}" -c "$cc" -o "$o"
    objs+=("$o")
done

echo "LD  $here/whd_gen"
"$CXX" $CXXFLAGS "${objs[@]}" -o "$here/whd_gen"

echo "Built $here/whd_gen"
