"""Append-only edits of an MHGU GUI file: new records go after the end of the file and the header points at the moved
arrays, so nothing already in it moves (records refer to each other by index, values by offset from a base)."""
import struct
from mtgui import Gui, REC, SECTIONS, HDR_FMT, HDR_NAMES


def align(b, a=16):
    b.extend(b'\0' * (-len(b) % a))


class Patch:
    def __init__(self, data):
        self.g = Gui(data)
        self.out = bytearray(data)
        self.recs = {k: [dict(r) for r in v] for k, v in self.g.rec.items()}
        self.moved = set()

    def s(self, off):
        a = self.g.h['stringOffset'] + off
        return self.out[a:self.out.index(b'\0', a)].decode('utf-8', 'replace')

    def by_name(self, kind, name):
        hits = [i for i, r in enumerate(self.recs[kind]) if self.s(r['name']) == name]
        assert len(hits) == 1, (kind, name, hits)
        return hits[0]

    def string(self, s):
        align(self.out, 4)
        off = len(self.out) - self.g.h['stringOffset']
        self.out += s.encode() + b'\0'
        return off

    def kv32_float(self, v):
        """The offset (from keyValue32Offset) of a float value: an existing one, else a new one at the end."""
        base, end = self.g.h['keyValue32Offset'], self.g.h['keyValue128Offset']
        want = struct.pack('<f', v)
        for o in range(base, end, 4):
            if self.g.data[o:o + 4] == want:
                return o - base
        align(self.out, 4)
        off = len(self.out) - base
        self.out += want
        return off

    def add(self, kind, rec):
        self.moved.add(kind)
        self.recs[kind].append(rec)
        return len(self.recs[kind]) - 1

    def clone_inits(self, thing, changes):
        """A copy of thing's init params at the end of the array, with {name: float} changed."""
        first = len(self.recs['init'])
        for p in self.recs['init'][thing['InitParamIndex']:thing['InitParamIndex'] + thing['InitParamNum']]:
            q = dict(p)
            n = self.s(p['name'])
            if n in changes:
                assert p['Type'] == 2, (n, p['Type'])
                q['value'] = self.kv32_float(changes[n])
            self.add('init', q)
        return first

    def set_float(self, kind, index, name, v):
        """Points one init param of a record at another float value (values are shared: never written over)."""
        r = self.recs[kind][index]
        for i in range(r['InitParamIndex'], r['InitParamIndex'] + r['InitParamNum']):
            p = self.recs['init'][i]
            if self.s(p['name']) == name:
                assert p['Type'] == 2
                p['value'] = self.kv32_float(v)
                self.moved.add('init')
                return
        raise KeyError(name)

    def clone_instance(self, src_name, new_name, after, changes):
        """A copy of instance src_name, named new_name, as the next sibling of instance `after`."""
        src = self.recs['inst'][self.by_name('inst', src_name)]
        a = self.by_name('inst', after)
        new = dict(src)
        new['ID'] = max(r['ID'] for r in self.recs['inst']) + 1
        new['name'] = self.string(new_name)
        new['InitParamIndex'] = self.clone_inits(src, changes)
        new['NextIndex'] = self.recs['inst'][a]['NextIndex']
        new['ChildIndex'] = -1
        i = self.add('inst', new)
        self.recs['inst'][a]['NextIndex'] = i
        # Each instance has one execution param, at the end of the params (instExeParamNum of them from
        # instExeParamOffset), told to belong to it by its ParentID (the instance's ID): a copy of the source's
        exe = self.g.h['instExeParamOffset']
        assert len(self.recs['param']) == exe + i, 'the execution params are not at the end of the params'
        own = [p for p in self.recs['param'][exe:] if p['ParentID'] == src['ID']]
        assert len(own) == 1, (src_name, len(own))
        param = dict(own[0])
        param['ParentID'] = new['ID']
        self.add('param', param)
        return i

    def set_param_floats(self, parent, name, old, new):
        """Points the float params `name` of the instance named `parent` that hold `old` at `new` (a flow's values)."""
        pid = self.recs['inst'][self.by_name('inst', parent)]['ID']
        base = self.g.h['keyValue32Offset']
        n = 0
        for p in self.recs['param']:
            if p['ParentID'] == pid and p['Type'] == 2 and p['ValueCount'] == 1 and self.s(p['name']) == name and \
                    abs(struct.unpack_from('<f', self.out, base + p['value'])[0] - old) < 1e-4:
                p['value'] = self.kv32_float(new)
                n += 1
        assert n, (parent, name, old)
        self.moved.add('param')
        return n

    def set_exe_flag(self, inst_name, index, on):
        """Shows (or hides) instance inst_name in one of the layout's states: its execution param holds a u8 per state
        (the flows in the order of their IDs). The values are shared, so the param gets its own copy, at the end."""
        iid = self.recs['inst'][self.by_name('inst', inst_name)]['ID']
        exe = self.g.h['instExeParamOffset']
        own = [p for p in self.recs['param'][exe:] if p['ParentID'] == iid]
        assert len(own) == 1 and own[0]['Type'] == 3, (inst_name, len(own))
        p = own[0]
        at = self.g.h['keyValue8Offset'] + p['value']
        flags = bytearray(self.out[at:at + p['ValueCount']])
        assert index < len(flags), (inst_name, index, len(flags))
        flags[index] = 1 if on else 0
        p['value'] = len(self.out) - self.g.h['keyValue8Offset']
        self.out += flags
        self.moved.add('param')

    def build(self):
        h = dict(self.g.h)
        # every array touched is written again after the end, in full
        for kind in sorted(self.moved):
            fmt, names = REC[kind]
            off, num = SECTIONS[kind]
            align(self.out, 16)
            h[off] = len(self.out)
            h[num] = len(self.recs[kind])
            for r in self.recs[kind]:
                self.out += struct.pack(fmt, *[r[n] for n in names.split()])
        if 'inst' in self.moved:
            h['instanceID'] = max(h['instanceID'], max(r['ID'] for r in self.recs['inst']) + 1)
            h['instExeParamNum'] = len(self.recs['inst'])  # (one per instance: an instance without one isn't run)
        align(self.out, 16)
        h['fileSize'] = len(self.out)
        struct.pack_into(HDR_FMT, self.out, 0, *[h[n] for n in HDR_NAMES])
        return bytes(self.out)
