ENTRY(DL_AppMain)

SECTIONS
{
    __dp = 0x00000000;
    . = 0x02700000;

    .startup :
    {
        KEEP(*(.text.startup))
    }

    .text :
    {
        *(.text .text.*)
    }

    .rodata :
    {
        *(.rodata .rodata.*)
    }

    .data :
    {
        *(.data .data.*)
    }

    . = ALIGN(4);
    __bss_start = .;
    .bss (NOLOAD) :
    {
        *(.bss .bss.*)
        *(COMMON)
    }
    . = ALIGN(4);
    __bss_end = .;

    .scratch (NOLOAD) :
    {
        . = ALIGN(4);
        __scratch_start = .;
        *(.scratch .scratch.*)
        __scratch_end = .;
    }

    /* KF2 only carries a flat payload length.  A real byte after the NOLOAD
       scratch range makes llvm-objcopy materialize the entire intervening
       range in the payload, so the 9288 loader reserves it. */
    . = ALIGN(4);
    .payload_tail :
    {
        BYTE(0xff)
    }
    __payload_end = .;
}
