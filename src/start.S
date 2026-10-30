@ NRO start: offset 0 is a branch, offset 4 the MOD0 offset, 0x10..0x80 the NRO header (written by elf2nro.py).
    .section .nro.start, "ax"
    .arm
    .global __nro_start
    .hidden __nro_start
    .hidden __bss_start, __bss_end, __eh_frame_hdr_start, __eh_frame_hdr_end, _DYNAMIC
__nro_start:
    b       .
    .word   __mod0 - __nro_start
    .space  0x78

@ MOD0: offsets relative to itself.
    .section .nro.mod0, "a"
    .align  2
__mod0:
    .ascii  "MOD0"
    .word   _DYNAMIC - __mod0
    .word   __bss_start - __mod0
    .word   __bss_end - __mod0
    .word   __eh_frame_hdr_start - __mod0
    .word   __eh_frame_hdr_end - __mod0
    .word   0
