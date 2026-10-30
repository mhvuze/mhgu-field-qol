"""Builds the mod's reward screen archives from your own game files (the repository holds no game data).

usage: python3 -I tools/make_arcs.py <recomp folder> [mod folder]

The reward screen's menu (eng\\GUI\\01_quest\\result\\reward) gets a fourth row under "Send to the Item Box" for the
plugin's "Send all to Item Box & Exit": a copy of the third row one row lower, a second separator line one row lower, a
taller window and the "Time Left" box under it one row lower. The plugin gives the row its text and its action. The
archive holding the layout is copied whole with that one file changed: a mod's archive replaces the game's (the recomp
merges two mods' archives, not a mod's and the game's).
"""
import os
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from patchgui import Patch

LAYOUT = 'GUI\\01_quest\\result\\reward'
GUI_TYPE = 0x22948394
ARCS = ['arc/result_%s.arc', 'arc/village/common_%s.arc']  # (both hold the reward layout)
LANGS = ['eng']

ROW_STEP = 34.0      # the rows are at 39, 73, 108
NEW_ROW_Y = 142.0
# The menu's states (flow params) size it for two or three rows: the window's SclY 5.8 / 7.5, the "Time Left" box
# under it at 188 / 222. Three rows become four:
WINDOW_SCALE_Y = (7.5, 9.2)
TIME_LEFT_Y = (222.0, 256.0)


def patch_layout(data):
    p = Patch(data)
    p.clone_instance('Anm_mesS02', 'Anm_mesS03', 'Anm_mesS02', {'PosY': NEW_ROW_Y})
    p.clone_instance('Anm_line_sub', 'Anm_line_sub_fq', 'Anm_line_sub', {'PosY': ROW_STEP})
    p.set_param_floats('Anm_Window_sub', 'SclY', *WINDOW_SCALE_Y)
    p.set_param_floats('Nul_sub1', 'PosY', *TIME_LEFT_Y)
    return p.build()


def patch_arc(arc, lang):
    """The archive with its reward layout patched, or None if it has none."""
    version, count = struct.unpack_from('<HH', arc, 4)
    entries = []
    for i in range(count):
        o = 12 + i * 0x50
        name = arc[o:o + 0x40].split(b'\0')[0].decode('latin1')
        typ, csize, sf, off = struct.unpack_from('<IIII', arc, o + 0x40)
        entries.append([name, typ, sf, arc[off:off + csize]])
    hits = [e for e in entries if e[1] == GUI_TYPE and e[0].endswith(LAYOUT)]
    if not hits:
        return None
    for e in hits:
        data = patch_layout(zlib.decompress(e[3]))
        e[3] = zlib.compress(data, 9)
        e[2] = (e[2] & ~0x1FFFFFFF) | len(data)
    head = 12 + count * 0x50
    out = bytearray(struct.pack('<4sHHI', b'ARC\0', version, count, 0))
    data_at = (head + 0x7FFF) // 0x8000 * 0x8000
    blobs = bytearray()
    for name, typ, sf, blob in entries:
        out += name.encode('latin1').ljust(0x40, b'\0') + struct.pack('<IIII', typ, len(blob), sf, data_at + len(blobs))
        blobs += blob
    out += b'\0' * (data_at - len(out))
    return bytes(out + blobs)


def main(recomp, mod):
    native = os.path.join(recomp, 'data', 'romfs', 'nativeNX')
    for lang in LANGS:
        for rel in ARCS:
            rel = rel % lang
            src = os.path.join(native, lang, rel)
            out = patch_arc(open(src, 'rb').read(), lang)
            if out is None:
                print('no reward layout in', src)
                continue
            dst = os.path.join(mod, 'romfs', 'nativeNX', lang, rel)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            open(dst, 'wb').write(out)
            print('wrote', dst)


if __name__ == '__main__':
    here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    main(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else here)
