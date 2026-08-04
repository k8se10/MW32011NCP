// host_stubs.cpp -- satisfies controller_input.cpp's own small set of extern
// symbols (real definitions split across analog_input_hooks.cpp/d3d9_hook.cpp in
// the real mod), which only feed IsControllerActiveInputMethod() -- not called by
// anything main.cpp or the hot-swapped ui_hot.dll actually needs.
#include <windows.h>
#include <cstdio>

void MarkControllerActivity() {}
extern "C" DWORD GetLastControllerActivityTickMs() { return 0; }
extern "C" DWORD GetLastMouseMoveTickMs() { return 0; }

// controller_input.cpp also logs XInput load failures via this -- host's own
// separate copy from ui_hot.dll's dll_stubs.cpp (the two never share a definition
// across the module boundary).
void LogFromController(const char* msg)
{
    printf("[ui_harness] %s\n", msg);
    OutputDebugStringA(msg);
    OutputDebugStringA("\n");
}
