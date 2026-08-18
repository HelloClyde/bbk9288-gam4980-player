#include "Dsys.h"

T_WORD App_Main(void)
{
    (void)fnGUI_MessageBox(
        HWND_DESKTOP,
        (const T_BYTE *)"S1C33 call ABI is working.",
        (const T_BYTE *)"ABI PROBE",
        MB_OK
    );
    return 0;
}
