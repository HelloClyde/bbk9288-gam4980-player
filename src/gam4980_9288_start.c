#include "Dsys.h"

T_GUI_RelocationTable *tpDL_GUITable;
T_CRTL_RelocationTable *tpDL_CRTLTable;
T_FS_RelocationTable *tpDL_FSTable;
T_ROS33_RelocationTable *tpDL_ROS33Table;
T_Audio_RelocationTable *tpDL_AudioTable;
T_Dict_RelocationTable *tpDL_DictTable;

extern unsigned char __bss_start;
extern unsigned char __bss_end;
extern T_WORD App_Main(void);

static void init_relocation_tables(void)
{
    T_GeneralRelocationTable *table =
        (T_GeneralRelocationTable *)SYS_GENERAL_RELOCATION_TABLE_ADDRESS;

    tpDL_ROS33Table =
        (T_ROS33_RelocationTable *)table->ROS33_RelocationAddress;
    tpDL_GUITable = (T_GUI_RelocationTable *)table->GUI_RelocationAddress;
    tpDL_FSTable = (T_FS_RelocationTable *)table->FS_RelocationAddress;
    tpDL_CRTLTable = (T_CRTL_RelocationTable *)table->CRTL_RelocationAddress;
    tpDL_AudioTable =
        (T_Audio_RelocationTable *)table->Audio_RelocationAddress;
    tpDL_DictTable = (T_Dict_RelocationTable *)table->Dict_RelocationAddress;
}

__attribute__((section(".text.startup")))
long DL_AppMain(long argument)
{
    unsigned char *cursor = &__bss_start;
    unsigned char *end = &__bss_end;

    (void)argument;
    while (cursor < end && ((unsigned long)cursor & 3u) != 0u)
        *cursor++ = 0;
    while (cursor + 32u <= end) {
        unsigned int *words = (unsigned int *)cursor;

        words[0] = 0;
        words[1] = 0;
        words[2] = 0;
        words[3] = 0;
        words[4] = 0;
        words[5] = 0;
        words[6] = 0;
        words[7] = 0;
        cursor += 32u;
    }
    while (cursor < end)
        *cursor++ = 0;
    init_relocation_tables();
    return (long)App_Main();
}
