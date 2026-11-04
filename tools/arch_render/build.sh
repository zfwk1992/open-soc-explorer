#!/usr/bin/env bash
# Builds build/arch_render/arch_render.exe directly with the toolchain's MinGW
# + Qt (no CMake) and compiles the archexplorer translation for --qm.
# Run from the repository root in Git Bash.
set -e
Q=${QTDIR:-/p/workspace/soc/toolchain/Qt/6.8.3/mingw_64}
export PATH="/p/workspace/soc/toolchain/Qt/Tools/mingw1310_64/bin:$Q/bin:$PATH"
B=build/arch_render && mkdir -p $B
INC="-I$Q/include -I$Q/include/QtCore -I$Q/include/QtGui -I$Q/include/QtWidgets -Isrc -Isrc/archexplorer"
moc src/archexplorer/archscene.h -o $B/moc_archscene.cpp
rcc --name arch resources/arch/arch.qrc -o $B/qrc_arch.cpp
for f in src/archexplorer/archmodel.cpp src/archexplorer/archscene.cpp $B/moc_archscene.cpp $B/qrc_arch.cpp tools/arch_render/arch_render_i18n.cpp tools/arch_render/arch_render.cpp; do
  o=$B/$(basename $f .cpp).o
  if [ ! -f $o ] || [ $f -nt $o ] || [ src/archexplorer/archscene.h -nt $o ] || [ src/archexplorer/archmodel.h -nt $o ]; then
    g++ -std=c++20 -O1 -fPIC $INC -c $f -o $o
  fi
done
g++ $B/*.o -L$Q/lib -lQt6Widgets -lQt6Gui -lQt6Core -o $B/arch_render.exe
lrelease -silent resources/i18n/archexplorer_zh_CN.ts -qm $B/archexplorer_zh_CN.qm
echo "built $B/arch_render.exe"
