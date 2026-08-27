//
// CNC3D — null keyboard backend for the headless "brain" build.
//
// Vanilla Conquer's remaster library (commonr) is already headless for audio and video:
// COMMONR_SRC pulls in soundio_null.cpp and video_null.cpp. The one thing it hardcodes is
// wwkeyboard_win32.cpp, which is the sole reason the remaster target refuses to compile
// off Windows (it needs ToAscii/UINT/PBYTE from windows.h).
//
// The remaster DLL is driven entirely by an external host through the CNC_* API --
// CNC_Handle_Input() feeds player commands in -- so it never needs to poll a real
// keyboard. Supplying a null backend therefore removes the LAST platform dependency and
// leaves a brain that is pure, portable C++.
//
// That matters beyond convenience: the same object files must later cross-compile to
// 32-bit Windows for the Win98/Voodoo2 target. Reaching for wwkeyboard_sdl2.cpp instead
// would have bound the game logic to SDL, which we do not want on Win98.
//
// WWKeyboardClass declares exactly two pure virtuals; this satisfies them and nothing else.
//
#include "wwkeyboard.h"

class WWKeyboardClassNull : public WWKeyboardClass
{
public:
    virtual ~WWKeyboardClassNull();
    virtual KeyASCIIType To_ASCII(unsigned short key);

protected:
    virtual void Fill_Buffer_From_System(void);
};

WWKeyboardClassNull::~WWKeyboardClassNull()
{
}

// No hardware to poll. The host injects input via CNC_Handle_Input().
void WWKeyboardClassNull::Fill_Buffer_From_System(void)
{
}

// Only the low byte is meaningful for the engine's own text handling; without a keymap
// there is nothing better to return, and the headless brain never types.
KeyASCIIType WWKeyboardClassNull::To_ASCII(unsigned short key)
{
    if (key & WWKEY_RLS_BIT) {
        return KA_NONE;
    }
    return (KeyASCIIType)(key & 0xFF);
}

WWKeyboardClass* CreateWWKeyboardClass(void)
{
    return new WWKeyboardClassNull;
}
