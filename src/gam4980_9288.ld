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

    .noinit (NOLOAD) :
    {
        . = ALIGN(4);
        *(.noinit .noinit.*)
    }
}
