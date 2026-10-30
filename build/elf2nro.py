"""Turn the linked ARM32 shared object into an NRO (text / ro / data segments plus a MOD0 header)."""
import struct
import sys

PAGE = 0x1000


def align(v, a=PAGE):
    return (v + a - 1) & ~(a - 1)


def main(src, dst):
    elf = open(src, 'rb').read()
    assert elf[:4] == b'\x7fELF' and elf[4] == 1, 'expected a 32-bit ELF'
    phoff, shoff = struct.unpack_from('<II', elf, 0x1c)
    phentsize, phnum, shentsize, shnum, shstrndx = struct.unpack_from('<HHHHH', elf, 0x2a)

    loads = []
    for i in range(phnum):
        p_type, p_off, p_vaddr, _, p_filesz, p_memsz, p_flags, _ = struct.unpack_from('<8I', elf, phoff + i * phentsize)
        if p_type == 1:
            loads.append((p_vaddr, p_off, p_filesz, p_memsz, p_flags))
    assert len(loads) == 3, 'expected text, ro and data segments, got %d' % len(loads)
    loads.sort()
    (t_va, t_off, t_sz, _, _), (r_va, r_off, r_sz, _, _), (d_va, d_off, d_sz, d_mem, _) = loads
    assert t_va == 0 and r_va % PAGE == 0 and d_va % PAGE == 0

    # Section lookup for .dynstr / .dynsym (offsets relative to the ro segment, as in libnx's NROs).
    shstr_off = struct.unpack_from('<I', elf, shoff + shstrndx * shentsize + 0x10)[0]
    secs = {}
    for i in range(shnum):
        name, _, _, addr, _, size = struct.unpack_from('<6I', elf, shoff + i * shentsize)
        end = elf.index(b'\0', shstr_off + name)
        secs[elf[shstr_off + name:end].decode()] = (addr, size)

    text_size = align(t_sz)
    ro_size = align(r_sz)
    data_file = d_sz
    data_size = align(data_file)
    bss_size = align(d_va + d_mem) - (d_va + data_size)
    if bss_size < 0:
        bss_size = 0
    total = d_va + data_size

    img = bytearray(total)
    img[t_va:t_va + t_sz] = elf[t_off:t_off + t_sz]
    img[r_va:r_va + r_sz] = elf[r_off:r_off + r_sz]
    img[d_va:d_va + d_sz] = elf[d_off:d_off + d_sz]
    assert text_size <= r_va and r_va + ro_size <= d_va

    dynstr = secs.get('.dynstr', (0, 0))
    dynsym = secs.get('.dynsym', (0, 0))
    hdr = struct.pack('<4sIII', b'NRO0', 0, total, 0)
    hdr += struct.pack('<6I', 0, r_va, r_va, d_va - r_va, d_va, data_size)
    hdr += struct.pack('<II', bss_size, 0)
    hdr += bytes(0x20)  # module id
    hdr += struct.pack('<II', 0, 0)
    hdr += struct.pack('<6I', 0, 0, dynstr[0] - r_va, dynstr[1], dynsym[0] - r_va, dynsym[1])
    assert len(hdr) == 0x70
    img[0x10:0x80] = hdr

    mod0 = struct.unpack_from('<I', img, 4)[0]
    assert img[mod0:mod0 + 4] == b'MOD0', 'MOD0 not where offset 4 says'
    open(dst, 'wb').write(img)
    print('%s: text %#x, ro %#x, data %#x, bss %#x' % (dst, text_size, d_va - r_va, data_size, bss_size))


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
