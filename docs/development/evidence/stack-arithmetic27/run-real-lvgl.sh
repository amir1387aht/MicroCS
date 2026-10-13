#!/bin/sh
set -eu
cd /data/MicroCS-v2-next
CMAKE=/data/toolchains/format-cmake/cmake/data/bin/cmake
export LD_LIBRARY_PATH=/data/toolchains/sanitizer-libs ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
for major in 8 9; do
  ver=$major.4.0
  if [ ! -d /data/deps/lvgl-$ver ]; then git clone --depth 1 --branch v$ver https://github.com/lvgl/lvgl.git /data/deps/lvgl-$ver; fi
  git -C /data/deps/lvgl-$ver rev-parse HEAD
  fix=OFF; [ "$major" = 8 ] && fix=ON
  "$CMAKE" -S . -B build/stack-lvgl$major -DMICROCS_CONFIG=NONE -DMICROCS_LVGL=ON -DMICROCS_LVGL_MAJOR=$major -DMICROCS_LVGL_ROOT=/data/deps/lvgl-$ver -DMICROCS_LVGL_CONFIG="$PWD/tests/lvgl/lv_conf${major}_fonts.h" -DMICROCS_LVGL8_FONT_DECODER_FIX=$fix -DCMAKE_C_FLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie' -DCMAKE_CXX_FLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie' -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined -no-pie'
  "$CMAKE" --build build/stack-lvgl$major --target mcs test_lvgl -j2
  build/stack-lvgl$major/test_lvgl
  for t in basic pointer constructors widgets fonts; do
    build/stack-lvgl$major/mcs tests/lvgl/$t.cs > build/stack-lvgl$major/$t.out
    cmp build/stack-lvgl$major/$t.out tests/lvgl/$t.out
    echo "PASS LVGL$major $t golden"
  done
done
