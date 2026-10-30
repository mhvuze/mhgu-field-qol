#!/bin/sh
# Builds the plugin, out/field_qol.nro.
#
#   sh build.sh            build only
#   sh build.sh install    build, then install the mod into the recomp's mods/field-qol/ (plugin and reward layout)
#   sh build.sh release    build, then make dist/field-qol-<version>.zip, the folder players put in mods/
#
# The reward layout is made from your own game files, so install and release need the recomp's folder: RECOMP=<path>
# (the folder holding data/ and mods/; on macOS ~/Library/Application Support/MHGURecomp, the default).
#
# Needs an arm-none-eabi GCC and binutils (Homebrew's arm-none-eabi-gcc; CROSS=<prefix> for another) and Python 3.
set -e
cd "$(dirname "$0")"
VERSION=0.5.0
CROSS=${CROSS:-arm-none-eabi-}
RECOMP=${RECOMP:-"$HOME/Library/Application Support/MHGURecomp"}
OUT=out
mkdir -p "$OUT"

# Hard float: the game takes floats in VFP registers (GUI positions). VFP only, no NEON.
CFLAGS="-march=armv7-a -marm -mfloat-abi=hard -mfpu=vfpv3-d16 -std=gnu11 -O2 -fPIC -ffreestanding -fno-builtin -nostdlib
        -fno-stack-protector -fvisibility=hidden -fno-unwind-tables -fno-asynchronous-unwind-tables -Wall -Wextra
        -DPLUGIN_VERSION=\"$VERSION\""
"${CROSS}gcc" $CFLAGS -c src/plugin.c -o "$OUT/plugin.o"
"${CROSS}gcc" $CFLAGS -c src/start.S -o "$OUT/start.o"
"${CROSS}ld" -shared -T build/nro.ld --hash-style=sysv -z max-page-size=0x1000 \
    -soname field_qol.nro "$OUT/start.o" "$OUT/plugin.o" -o "$OUT/field_qol.elf"
python3 -I build/elf2nro.py "$OUT/field_qol.elf" "$OUT/field_qol.nro"

# The mod's folder: the plugin, the reward layout and a short readme
make_mod() {
    mkdir -p "$1/romfs/nativeNX/plugins"
    cp "$OUT/field_qol.nro" "$1/romfs/nativeNX/plugins/"
    python3 -I tools/make_arcs.py "$RECOMP" "$1"
    sed "s/@VERSION@/$VERSION/" release/README.txt > "$1/README.txt"
}

case "$1" in
install)
    make_mod "$RECOMP/mods/field-qol"
    echo "installed: $RECOMP/mods/field-qol"
    ;;
release)
    rm -rf "$OUT/release" && mkdir -p "$OUT/release" dist
    make_mod "$OUT/release/field-qol"
    rm -f "dist/field-qol-$VERSION.zip"
    (cd "$OUT/release" && zip -qrX "../../dist/field-qol-$VERSION.zip" field-qol)
    echo "made: dist/field-qol-$VERSION.zip"
    ;;
esac
