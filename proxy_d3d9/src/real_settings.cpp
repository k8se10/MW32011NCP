// real_settings.cpp -- see real_settings.h for the full rationale and the raw
// disassembly trail behind each address (re_notes/options_menu_full_map.md).
#include <windows.h>
#include <cstring>
#include "real_settings.h"

// ---- Dvar setters ----------------------------------------------------------------
//
// All three confirmed via raw disassembly (options_menu_full_map.md sec 4/6) to be
// plain __cdecl functions -- normal stack-passed args, standard prologue/epilogue --
// unlike the raw Dvar_FindVar internals (FUN_0062abe0) these call internally, which
// use a custom EDI-register name-argument convention. No inline asm needed here.
namespace {
using SetDvarBoolFn = void(__cdecl*)(const char*, int);
SetDvarBoolFn const SetDvarBoolRaw = reinterpret_cast<SetDvarBoolFn>(0x0044d700);

using SetDvarStringFn = void(__cdecl*)(const char*, const char*);
SetDvarStringFn const SetDvarStringRaw = reinterpret_cast<SetDvarStringFn>(0x005396b0);

using SetDvarFloatFn = void(__cdecl*)(const char*, float);
SetDvarFloatFn const SetDvarFloatRaw = reinterpret_cast<SetDvarFloatFn>(0x005513c0);

// Key_CommandStringToId-equivalent (options_menu_full_map.md sec 7: the same 81-entry
// canonical bind-name table this project's own Sprint/weapnext RE work already found
// -- FUN_005330a0 IS that table's real string->ID resolver). Plain __cdecl, single
// string arg, confirmed via raw disassembly. Returns 0 if the command string isn't a
// real recognized bind.
using CommandStringToIdFn = int(__cdecl*)(const char*);
CommandStringToIdFn const CommandStringToId = reinterpret_cast<CommandStringToIdFn>(0x005330a0);

// Key_SetBinding-equivalent: writes commandId directly into the real keybind table
// slot for (configIndex, keynum). Plain __cdecl, confirmed via raw disassembly
// (real formula: configIndex*0xd28 + keynum*3*4 + 0xa98e4c).
using SetKeybindRawFn = void(__cdecl*)(int configIndex, int keynum, int commandId);
SetKeybindRawFn const SetKeybindRaw = reinterpret_cast<SetKeybindRawFn>(0x0044a900);

using KeyNameToKeynumFn = int(__cdecl*)(const char*);
KeyNameToKeynumFn const KeyNameToKeynumRaw = reinterpret_cast<KeyNameToKeynumFn>(0x00508e70);

using KeynumToDisplayNameFn = void(__cdecl*)(int keynum, char* outBuf, int outBufSize);
KeynumToDisplayNameFn const KeynumToDisplayNameRaw = reinterpret_cast<KeynumToDisplayNameFn>(0x004bea00);

// Cbuf_AddText-equivalent -- see header comment.
using CbufAddTextFn = void(__cdecl*)(int localClientNum, const char*);
CbufAddTextFn const CbufAddText = reinterpret_cast<CbufAddTextFn>(0x00457c90);

// SEH_GetString-equivalent (issue #68, 2026-08-05 language pass). Confirmed via raw
// disassembly of FUN_00532230 (re_notes/ghidra_scripts/DumpDisasm.java output): plain
// __cdecl, single stack arg read at [ESP+8], plain `RET` (not `RET 4`) -- genuinely
// caller-cleans-the-stack, unlike this file's custom-register Dvar/keybind internals.
// Found by decompiling FUN_00568110 (the real weapon-pickup/swap hint builder,
// re_notes/ui_assets.md's own hint-text survey) -- it passes literal reference-key
// strings like "PLATFORM_PICKUPNEWWEAPON" directly into this function and gets back
// the resolved display text for splicing into the "&&1" template engine. Internally:
// if the key starts with the real 0x15 "already-literal" escape byte, skips lookup
// entirely; otherwise calls FUN_0046df70 (the actual reference->current-language-
// string table lookup) and, if that returns null (key not found), falls back to
// echoing the raw key string itself rather than returning null -- so this is always
// safe to call and never needs a null check on the caller's side.
using GetLocalizedStringFn = const char*(__cdecl*)(const char*);
GetLocalizedStringFn const GetLocalizedStringRaw = reinterpret_cast<GetLocalizedStringFn>(0x00532230);
} // namespace

// Raw Dvar_FindVar-equivalent (FUN_0062abe0) -- custom EDI-register name-argument
// convention, same as analog_input_hooks.cpp's own GetDvarInt/GetDvarFloat. Kept
// private to this file; GetDvarBool/GetDvarFloat/GetDvarString below are the public
// surface.
namespace {
void* FindDvar(const char* name)
{
    constexpr uintptr_t kFindDvarFn = 0x0062abe0;
    void* dvarPtr = nullptr;
#ifdef _M_IX86
    __asm {
        push edi
        mov edi, name
        mov eax, kFindDvarFn
        call eax
        mov dvarPtr, eax
        pop edi
    }
#endif
    return dvarPtr;
}
} // namespace

int GetDvarBool(const char* name)
{
    void* dvarPtr = FindDvar(name);
    if (!dvarPtr) return 0;
    return *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(dvarPtr) + 0xc);
}

float GetDvarFloat(const char* name)
{
    void* dvarPtr = FindDvar(name);
    if (!dvarPtr) return 0.0f;
    return *reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(dvarPtr) + 0xc);
}

const char* GetDvarString(const char* name)
{
    void* dvarPtr = FindDvar(name);
    if (!dvarPtr) return nullptr;
    return *reinterpret_cast<const char**>(reinterpret_cast<uintptr_t>(dvarPtr) + 0xc);
}

extern "C" void SetDvarBool(const char* name, int value) { SetDvarBoolRaw(name, value); }
extern "C" void SetDvarString(const char* name, const char* value) { SetDvarStringRaw(name, value); }
extern "C" void SetDvarFloat(const char* name, float value) { SetDvarFloatRaw(name, value); }

// GetKeybind -- custom register calling convention, confirmed via raw disassembly of
// FUN_0057e640 (options_menu_full_map.md sec 7): EAX=command string, ECX=configIndex,
// EBX=&outKeynums[2] (the callee itself initializes both slots to -1 before scanning,
// so an unbound command correctly leaves both entries -1). Returns the real match
// count (0/1/2) in EAX. EBX/ESI/EDI are explicitly saved/restored around the call,
// same pattern GetDvarInt (analog_input_hooks.cpp) already uses for FUN_0062abe0 --
// the compiler assumes these are preserved across an inline-asm block and does not
// itself know this callee clobbers them.
int GetKeybind(const char* command, int configIndex, int outKeynums[2])
{
    constexpr uintptr_t kGetKeybindFn = 0x0057e640;
    int count = 0;
#ifdef _M_IX86
    __asm {
        push ebx
        push esi
        push edi
        mov eax, command
        mov ecx, configIndex
        mov ebx, outKeynums
        mov edx, kGetKeybindFn
        call edx
        mov count, eax
        pop edi
        pop esi
        pop ebx
    }
#endif
    return count;
}

void SetKeybind(const char* command, int configIndex, int keynum)
{
    int commandId = CommandStringToId(command);
    if (commandId == 0) return; // unrecognized command -- matches the real "bind" command's own silent no-op
    SetKeybindRaw(configIndex, keynum, commandId);
}

void UnbindKeynum(int keynum, int configIndex)
{
    // Confirmed equivalent to the real "unbind" handler's own effect on the table
    // (options_menu_full_map.md sec 7: it writes 0 into this exact slot) -- reusing
    // the real setter with commandId=0 rather than re-deriving the unbind handler's
    // own address computation separately.
    SetKeybindRaw(configIndex, keynum, 0);
}

int KeyNameToKeynum(const char* keyName)
{
    return KeyNameToKeynumRaw(keyName);
}

void KeynumToDisplayName(int keynum, char* outBuf, int outBufSize)
{
    if (!outBuf || outBufSize <= 0) return;
    // Real function's own internal buffer convention uses 0x80 -- enforce that floor
    // here rather than trusting an undersized caller buffer, since the real function
    // does not itself take a trustworthy bounds parameter (see header comment).
    if (outBufSize < 0x80) {
        char safeBuf[0x80] = {};
        KeynumToDisplayNameRaw(keynum, safeBuf, sizeof(safeBuf));
        strncpy_s(outBuf, static_cast<size_t>(outBufSize), safeBuf, _TRUNCATE);
        return;
    }
    KeynumToDisplayNameRaw(keynum, outBuf, outBufSize);
}

void QueueConsoleCommand(const char* command)
{
    CbufAddText(0, command);
}

const char* GetLocalizedString(const char* referenceKey)
{
    return GetLocalizedStringRaw(referenceKey);
}
