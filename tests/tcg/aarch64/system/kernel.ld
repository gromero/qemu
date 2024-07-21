ENTRY(__start)

SECTIONS
{
    /* virt machine, RAM starts at 1gb */
    . = (1 << 30);
    .text : {
        *(.text)
    }
    .rodata : {
        *(.rodata)
    }
    /* align r/w section to next 2mb */
    . = ALIGN(1 << 21);
    .data : {
        *(.data)
    }
    .bss : {
        *(.bss)
    }
    /* align MTE section to next (third) 2mb */
    . = ALIGN(1 << 22);
    .mte : {
        *(.mte_page)
    }
    /DISCARD/ : {
        *(.ARM.attributes)
    }
}
