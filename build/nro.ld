/* One image from address 0: text, then read-only data, then data/bss, each page aligned (NRO segments). */
PHDRS
{
    text PT_LOAD FLAGS(5);
    ro   PT_LOAD FLAGS(4);
    data PT_LOAD FLAGS(6);
    dyn  PT_DYNAMIC;
}

SECTIONS
{
    . = 0;
    .text : {
        KEEP(*(.nro.start))
        *(.text .text.*)
    } :text
    .plt : { *(.plt) } :text

    . = ALIGN(0x1000);
    __ro_start = .;
    .rodata : {
        KEEP(*(.nro.mod0))
        *(.rodata .rodata.*)
    } :ro
    .dynsym   : { *(.dynsym) } :ro
    .dynstr   : { *(.dynstr) } :ro
    .hash     : { *(.hash) } :ro
    .gnu.hash : { *(.gnu.hash) } :ro
    .rel.dyn  : { *(.rel.dyn) } :ro
    .rel.plt  : { *(.rel.plt) } :ro
    .ARM.exidx : { *(.ARM.exidx*) } :ro
    .eh_frame_hdr : { __eh_frame_hdr_start = .; *(.eh_frame_hdr) __eh_frame_hdr_end = .; } :ro
    .eh_frame : { *(.eh_frame) } :ro

    . = ALIGN(0x1000);
    __data_start = .;
    .data.rel.ro : { *(.data.rel.ro .data.rel.ro.*) } :data
    .dynamic : { *(.dynamic) } :data :dyn
    .got     : { *(.got) } :data
    .got.plt : { *(.got.plt) } :data
    .data    : { *(.data .data.*) } :data
    . = ALIGN(8);
    __bss_start = .;
    .bss : { *(.bss .bss.*) *(COMMON) } :data
    . = ALIGN(8);
    __bss_end = .;

    /DISCARD/ : { *(.comment) *(.note*) *(.ARM.attributes) }
}
