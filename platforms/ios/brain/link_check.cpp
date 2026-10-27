/* Link the real brain and every export, not merely an empty archive. This
 * executable is unsigned build evidence; it is not a device-deliverable app. */
#include "static_brain.h"
#include <cstdio>
int main()
{
    typedef unsigned int (*Version)(unsigned int);
    Version version = reinterpret_cast<Version>(opencnc3d_ios_brain_symbol("CNC_Version"));
    if (!version || !opencnc3d_ios_brain_symbol("CNC_Init") ||
        !opencnc3d_ios_brain_symbol("CNC_Start_Custom_Instance") ||
        opencnc3d_ios_brain_symbol("ThisExportMustNotExist")) return 1;
    std::printf("Tiberian Dawn static iOS brain: %zu exports; API %u\n",
                opencnc3d_ios_brain_symbol_count(), version(0));
    return 0;
}
