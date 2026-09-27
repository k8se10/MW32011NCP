// dvar_write_x64.cpp -- 2026-09-27, the real x64 dvar WRITE path (known_issues_x64.md
// issue #6: ForceAnisotropicFiltering/ForceHighQualityShadows/ForceHighQualityLighting
// and the Custom Options screen's vanilla-setting writes were silent no-ops on x64
// because real_settings.cpp's setters had to be stubbed out -- their x86 function
// pointers would crash in iw5sp.exe x64).
//
// ---- RE trail (iw5sp.exe x64, full write-up: re_notes/x64_migration/dvar_write_path_x64.md)
//
// Found by walking DOWN from two known SET entry points instead of UP from
// Dvar_FindVar's getter cluster:
//   * the menu-script "setdvar" handler (script command table entry at 0x140434270:
//     {"setdvar", FUN_1402a3180}) ends in FUN_1402c5900(name, valueString);
//   * FUN_1402c5900 = Dvar_SetFromStringByName: Dvar_FindVar (FUN_1402c3890) -> if
//     found, Dvar_SetFromStringFromSource (FUN_1402c5a30)(dvar, str, source 0); if not,
//     Dvar_RegisterString(name, 7, 0x100 external, str, "External Dvar").
// The earlier "19 call sites, all getters" survey missed these because
// Dvar_FindVar has a second entry: a 5-byte JMP thunk at 0x1402c3980 that 30 more
// callers use, and the setters' own FindVar calls sit further down the same
// function cluster (0x1402c52b0..0x1402c6272).
//
// Every setter funnels into ONE sink, FUN_1402c5f30 = Dvar_SetVariant(dvar,
// const DvarValue* value, int source). The typed setters used here (all take a
// cached dvar_t*, not a name; each has a SetXxxByName twin that also creates the
// dvar when missing, deliberately NOT used -- a typo would register a stray
// external dvar):
//   FUN_1402c52b0  Dvar_SetBool(dvar, bool)          type 0 direct; else "1"/"0" string
//   FUN_1402c5ad0  Dvar_SetInt(dvar, int)            types 5/6 direct; else "%i" string
//   FUN_1402c5700  Dvar_SetFloat(dvar, float)        type 1 direct; else "%g" string
//   FUN_1402c5a30  Dvar_SetFromStringFromSource(dvar, str, source)  parses per type
// All four pass source 0 (DVAR_SOURCE_INTERNAL) except the last, which takes it.
//
// TYPE MISMATCH TRAP: when the dvar is not the setter's native type, Dvar_SetBool/
// Dvar_SetInt/Dvar_SetFloat do NOT convert -- they sprintf the value and hand
// Dvar_SetVariant a DvarValue whose .string is that buffer. Only a string dvar can
// consume that. For an int dvar, the domain check (FUN_1402c6970) compares the raw
// 32-bit slot -- the low half of a stack pointer -- against [min, max], and the write
// is silently rejected. That is exactly ForceAnisotropicFiltering's case:
// r_texFilterAnisoMax/Min are Dvar_RegisterInt (FUN_1402c46d0, range 1..16), not
// floats, so SetDvarFloat(..., 16.0f) through Dvar_SetFloat could never land. So the
// drain below dispatches on the dvar's REAL type tag: the native setter when the types
// agree, a numeric conversion between bool/int/float, and Dvar_SetFromStringFromSource
// (which parses via Dvar_StringToValue, FUN_1402c6670) for everything else.
//
// dvar_t layout (x64, stride 0x60, read directly out of Dvar_SetVariant):
//   +0x00 name  +0x08 flags  +0x0C type(u8)  +0x10 current  +0x20 latched
//   +0x30 reset  +0x40 domain (enum: +0x40 count, +0x48 strings)  +0x50 domainFunc
// Type tags: 0 bool, 1 float, 2 vec2, 3 vec3, 4 vec4, 5 int, 6 enum, 7 string,
// 8 color, 9 vec3-color.
//
// ---- Why every write is queued to the main thread
// Dvar_SetVariant contains a thread gate: when (dvar->flags & ~tls[+0x24]) != 0 it
// calls FUN_14024a250 (GetCurrentThreadId() == the engine's main thread id) and, on
// any other thread, RETURNS WITHOUT WRITING -- no error, no log. Almost every dvar
// worth writing has flags (archive, latched, ...), and every caller of these setters
// runs on the render thread (Hook_EndScene) or the input/WndProc thread. That gate is
// what silently ate the F4 ai_disableSpawn toggle on 2026-09-21 (SetDvarIntX64's
// original comment). So Queue* only records the write, and Hook_ComFrameBodyX64
// applies it on the main thread.
//
// Drain point: FUN_14023cf20 = the Com_Frame body, called once per frame from the
// setjmp wrapper Com_Frame (FUN_14023ce80, sole caller: the main loop FUN_1402ef310).
// It runs in menus and in gameplay alike, unlike Hook_MovementTick (the old drain,
// which stops in the pause menu). It is also exactly the right spot for archived
// dvars: the body's FIRST action is "if (tls modified flags & DVAR_ARCHIVE) write
// players2/config.cfg" -- so a write drained at its top is persisted in the same
// frame, the same way a console "set" executed during the previous frame would be.
//
// SP only (InstallDvarWriteX64 is called from the SP-only InstallAnalogInputHooksX64).
// The four setter signatures are unique in iw5mp.exe too (twins at 0x140326330/
// 0x140326a60/0x140326720/0x1403269c0), but the Com_Frame-body signature does not
// match there -- MP's frame function is laid out differently and has not been RE'd.
// Without a main-thread drain the writes would never apply, so in MP every Queue*
// call is rejected with a logged reason rather than queued forever.

#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cmath>
#include "../third_party/minhook/include/MinHook.h"
#include "signature_scan.h"
#include "dvar_write_x64.h"

extern void LogFromController(const char* msg); // dllmain.cpp
extern "C" void* FindDvarX64_Exported(const char* name); // analog_input_hooks_x64.cpp
extern "C" bool IsFindDvarX64Resolved();                  // analog_input_hooks_x64.cpp

namespace {

// ---- Signatures (all verified unique in iw5sp.exe; see header comment) --------------
// Dvar_SetBool @0x1402c52b0: SUB RSP,0x38; CMP byte [RCX+0xC],0; JNZ; MOV [RSP+0x20],DL;
// JMP; TEST DL,DL; LEA R8,"1"; LEA RAX,"0"; CMOVNZ; ...; XOR R8D,R8D (source 0).
constexpr const char* kDvarSetBoolX64Signature =
    "48 83 EC 38 80 79 0C 00 75 06 88 54 24 20 EB ?? 84 D2 4C 8D 05 ?? ?? ?? ?? "
    "48 8D 05 ?? ?? ?? ?? 49 0F 45 C0 48 89 44 24 20 0F 28 44 24 20 48 8D 54 24 20 45 33 C0";
// Dvar_SetFloat @0x1402c5700: PUSH RBX; SUB RSP,0x50; CMP byte [RCX+0xC],1; MOV RBX,RCX;
// JNZ; MOVSS [RSP+0x20],XMM1; JMP; XORPS; LEA R8,"%g"; CVTSS2SD XMM3,XMM1; MOV EDX,0x20.
constexpr const char* kDvarSetFloatX64Signature =
    "40 53 48 83 EC 50 80 79 0C 01 48 8B D9 75 08 F3 0F 11 4C 24 20 EB ?? 0F 57 DB "
    "4C 8D 05 ?? ?? ?? ?? F3 0F 5A D9 BA 20 00 00 00 48 8D 4C 24 30";
// Dvar_SetInt @0x1402c5ad0: PUSH RBX; SUB RSP,0x50; MOVZX EAX,byte [RCX+0xC]; MOV RBX,RCX;
// SUB AL,5; CMP AL,1; JBE; MOV R9D,EDX; LEA R8,"%i"; MOV EDX,0x20; LEA RCX,[RSP+0x30]; CALL.
constexpr const char* kDvarSetIntX64Signature =
    "40 53 48 83 EC 50 0F B6 41 0C 48 8B D9 2C 05 3C 01 76 ?? 44 8B CA 4C 8D 05 ?? ?? ?? ?? "
    "BA 20 00 00 00 48 8D 4C 24 30 E8";
// Dvar_SetFromStringFromSource @0x1402c5a30: MOV [RSP+8],RBX; PUSH RDI; SUB RSP,0x440;
// MOV EDI,R8D; MOV RBX,RCX; MOV R8D,0x400; LEA RCX,[RSP+0x40]; CALL strncpyz;
// MOVUPS XMM0,[RBX+0x40]; MOVZX EDX,byte [RBX+0xC].
constexpr const char* kDvarSetFromStringX64Signature =
    "48 89 5C 24 08 57 48 81 EC 40 04 00 00 41 8B F8 48 8B D9 41 B8 00 04 00 00 "
    "48 8D 4C 24 40 E8 ?? ?? ?? ?? 0F 10 43 40 0F B6 53 0C";
// Com_Frame body @0x14023cf20: MOV [RSP+8],RBX; MOV [RSP+0x10],RBP; MOV [RSP+0x18],RSI;
// PUSH RDI; SUB RSP,0x80; MOV EAX,[rip+x]; MOVAPS [RSP+0x70],XMM6; MOVAPS [RSP+0x60],XMM7;
// TEST EAX,EAX; JZ near.
constexpr const char* kComFrameBodyX64Signature =
    "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 48 81 EC 80 00 00 00 8B 05 ?? ?? ?? ?? "
    "0F 29 74 24 70 0F 29 7C 24 60 85 C0 0F 84";

using DvarSetBoolFn = void (*)(void* dvar, bool value);
using DvarSetIntFn = void (*)(void* dvar, int value);
using DvarSetFloatFn = void (*)(void* dvar, float value);
using DvarSetFromStringFn = void (*)(void* dvar, const char* value, int source);
using ComFrameBodyFn = void (*)();

constexpr int kDvarSourceInternal = 0;

DvarSetBoolFn g_dvarSetBool = nullptr;
DvarSetIntFn g_dvarSetInt = nullptr;
DvarSetFloatFn g_dvarSetFloat = nullptr;
DvarSetFromStringFn g_dvarSetFromString = nullptr;
ComFrameBodyFn g_origComFrameBody = nullptr;
volatile bool g_dvarWriteAvailable = false;

// dvar_t field offsets / type tags -- see header comment.
constexpr uintptr_t kDvarTypeOffset = 0x0C;
constexpr uintptr_t kDvarValueOffset = 0x10;
constexpr uintptr_t kDvarEnumCountOffset = 0x40;
constexpr uintptr_t kDvarEnumStringsOffset = 0x48;
constexpr unsigned char kDvarTypeBool = 0;
constexpr unsigned char kDvarTypeFloat = 1;
constexpr unsigned char kDvarTypeInt = 5;
constexpr unsigned char kDvarTypeEnum = 6;
constexpr unsigned char kDvarTypeString = 7;

enum class DvarWriteKindX64 : unsigned char { Bool, Int, Float, String };

constexpr size_t kMaxDvarNameLen = 64;     // including terminator
constexpr size_t kMaxDvarStringLen = 256;  // including terminator
constexpr int kMaxPendingDvarWrites = 64;

struct PendingDvarWriteX64 {
    char name[kMaxDvarNameLen];
    DvarWriteKindX64 kind;
    bool boolValue;
    int intValue;
    float floatValue;
    char stringValue[kMaxDvarStringLen];
};

SRWLOCK g_pendingLock = SRWLOCK_INIT;
PendingDvarWriteX64 g_pending[kMaxPendingDvarWrites];
int g_pendingCount = 0;                 // guarded by g_pendingLock
volatile LONG g_pendingNonEmpty = 0;    // lock-free fast-path hint for the per-frame drain
PendingDvarWriteX64 g_drainBuffer[kMaxPendingDvarWrites]; // main thread (drain) only

// One log line per distinct rejection reason, so a caller that retries every frame
// cannot flood the log.
volatile LONG g_loggedUnavailable = 0;
volatile LONG g_loggedBadName = 0;
volatile LONG g_loggedBadString = 0;
volatile LONG g_loggedQueueFull = 0;

void LogOnce(volatile LONG* flag, const char* msg)
{
    if (InterlockedCompareExchange(flag, 1, 0) == 0) LogFromController(msg);
}

bool ValidateDvarName(const char* name)
{
    if (name && name[0] != '\0' && strnlen(name, kMaxDvarNameLen) < kMaxDvarNameLen) return true;
    LogOnce(&g_loggedBadName, "[x64-dvarwrite] rejected a write: dvar name null, empty, or longer than 63 chars");
    return false;
}

// Coalesces by name: a later write to the same dvar before the next drain replaces
// the earlier one (last writer wins, the same result applying both in order gives).
bool Enqueue(const PendingDvarWriteX64& entry)
{
    if (!g_dvarWriteAvailable) {
        LogOnce(&g_loggedUnavailable, "[x64-dvarwrite] rejected a write: the dvar write path is unavailable in this "
            "exe (signature miss, or not iw5sp.exe) -- see the [x64-dvarwrite] install lines");
        return false;
    }
    bool queued = false;
    AcquireSRWLockExclusive(&g_pendingLock);
    int slot = -1;
    for (int i = 0; i < g_pendingCount; ++i) {
        if (_stricmp(g_pending[i].name, entry.name) == 0) { slot = i; break; }
    }
    if (slot < 0 && g_pendingCount < kMaxPendingDvarWrites) slot = g_pendingCount++;
    if (slot >= 0) {
        g_pending[slot] = entry;
        InterlockedExchange(&g_pendingNonEmpty, 1);
        queued = true;
    }
    ReleaseSRWLockExclusive(&g_pendingLock);
    if (!queued) {
        LogOnce(&g_loggedQueueFull, "[x64-dvarwrite] rejected a write: 64 distinct dvars already pending this frame");
    }
    return queued;
}

// Formats the dvar's live value per its type tag. Strings come straight from engine
// memory; the caller's buffer bounds every copy.
void FormatDvarValue(const void* dvar, char* out, size_t outSize)
{
    const uintptr_t d = reinterpret_cast<uintptr_t>(dvar);
    const unsigned char type = *reinterpret_cast<const unsigned char*>(d + kDvarTypeOffset);
    switch (type) {
        case kDvarTypeBool:
            sprintf_s(out, outSize, "%d", *reinterpret_cast<const unsigned char*>(d + kDvarValueOffset) ? 1 : 0);
            break;
        case kDvarTypeFloat:
            sprintf_s(out, outSize, "%g", *reinterpret_cast<const float*>(d + kDvarValueOffset));
            break;
        case kDvarTypeInt:
            sprintf_s(out, outSize, "%d", *reinterpret_cast<const int*>(d + kDvarValueOffset));
            break;
        case kDvarTypeEnum: {
            const int index = *reinterpret_cast<const int*>(d + kDvarValueOffset);
            const int count = *reinterpret_cast<const int*>(d + kDvarEnumCountOffset);
            const char* const* strings = *reinterpret_cast<const char* const* const*>(d + kDvarEnumStringsOffset);
            const char* label = (strings && index >= 0 && index < count) ? strings[index] : nullptr;
            sprintf_s(out, outSize, "%d (%.64s)", index, label ? label : "?");
            break;
        }
        case kDvarTypeString: {
            const char* s = *reinterpret_cast<const char* const*>(d + kDvarValueOffset);
            sprintf_s(out, outSize, "\"%.96s\"", s ? s : "");
            break;
        }
        default:
            sprintf_s(out, outSize, "<type %u>", static_cast<unsigned>(type));
            break;
    }
}

// The requested value as each numeric dvar type stores it -- shared by the dispatch
// and the read-back check so both agree on the conversion.
bool RequestedAsBool(const PendingDvarWriteX64& w)
{
    switch (w.kind) {
        case DvarWriteKindX64::Bool:  return w.boolValue;
        case DvarWriteKindX64::Int:   return w.intValue != 0;
        case DvarWriteKindX64::Float: return w.floatValue != 0.0f;
        default:                      return false;
    }
}
int RequestedAsInt(const PendingDvarWriteX64& w)
{
    switch (w.kind) {
        case DvarWriteKindX64::Bool:  return w.boolValue ? 1 : 0;
        case DvarWriteKindX64::Int:   return w.intValue;
        case DvarWriteKindX64::Float: return static_cast<int>(lroundf(w.floatValue));
        default:                      return 0;
    }
}
float RequestedAsFloat(const PendingDvarWriteX64& w)
{
    switch (w.kind) {
        case DvarWriteKindX64::Bool:  return w.boolValue ? 1.0f : 0.0f;
        case DvarWriteKindX64::Int:   return static_cast<float>(w.intValue);
        case DvarWriteKindX64::Float: return w.floatValue;
        default:                      return 0.0f;
    }
}

// Writes w into dvar through the setter that matches the dvar's REAL type (see the
// TYPE MISMATCH TRAP in the header comment -- never let a typed setter take its
// string fallback on a non-string dvar).
void DispatchTypedWrite(void* dvar, const PendingDvarWriteX64& w)
{
    const unsigned char type =
        *reinterpret_cast<const unsigned char*>(reinterpret_cast<uintptr_t>(dvar) + kDvarTypeOffset);
    if (w.kind == DvarWriteKindX64::String) {
        g_dvarSetFromString(dvar, w.stringValue, kDvarSourceInternal);
        return;
    }
    switch (type) {
        case kDvarTypeBool:  g_dvarSetBool(dvar, RequestedAsBool(w)); return;
        case kDvarTypeFloat: g_dvarSetFloat(dvar, RequestedAsFloat(w)); return;
        case kDvarTypeInt:
        case kDvarTypeEnum:  g_dvarSetInt(dvar, RequestedAsInt(w)); return;
        default: break;
    }
    // Vector/color/string dvar given a scalar: let the engine parse its text form.
    char text[32];
    switch (w.kind) {
        case DvarWriteKindX64::Bool:  sprintf_s(text, "%d", w.boolValue ? 1 : 0); break;
        case DvarWriteKindX64::Int:   sprintf_s(text, "%d", w.intValue); break;
        case DvarWriteKindX64::Float: sprintf_s(text, "%g", w.floatValue); break;
        default:                      text[0] = '\0'; break;
    }
    g_dvarSetFromString(dvar, text, kDvarSourceInternal);
}

// Whether the read-back equals what was requested, where that is well defined.
// Returns -1 when the comparison does not apply (e.g. a scalar written to a vector,
// or a string parsed into a numeric dvar).
int ReadBackMatches(const PendingDvarWriteX64& w, const void* dvar)
{
    const uintptr_t d = reinterpret_cast<uintptr_t>(dvar);
    const unsigned char type = *reinterpret_cast<const unsigned char*>(d + kDvarTypeOffset);
    if (w.kind == DvarWriteKindX64::String) {
        if (type != kDvarTypeString) return -1;
        const char* s = *reinterpret_cast<const char* const*>(d + kDvarValueOffset);
        return (s && strcmp(s, w.stringValue) == 0) ? 1 : 0;
    }
    switch (type) {
        case kDvarTypeBool:
            return (*reinterpret_cast<const unsigned char*>(d + kDvarValueOffset) != 0) == RequestedAsBool(w);
        case kDvarTypeFloat:
            return *reinterpret_cast<const float*>(d + kDvarValueOffset) == RequestedAsFloat(w);
        case kDvarTypeInt:
        case kDvarTypeEnum:
            return *reinterpret_cast<const int*>(d + kDvarValueOffset) == RequestedAsInt(w);
        default:
            return -1;
    }
}

void FormatRequested(const PendingDvarWriteX64& w, char* out, size_t outSize)
{
    switch (w.kind) {
        case DvarWriteKindX64::Bool:   sprintf_s(out, outSize, "%d", w.boolValue ? 1 : 0); break;
        case DvarWriteKindX64::Int:    sprintf_s(out, outSize, "%d", w.intValue); break;
        case DvarWriteKindX64::Float:  sprintf_s(out, outSize, "%g", w.floatValue); break;
        case DvarWriteKindX64::String: sprintf_s(out, outSize, "\"%.96s\"", w.stringValue); break;
    }
}

// Applies one write on the main thread. SEH-guarded: engine code and engine memory
// are on the other side of every call here. No C++ objects with destructors in this
// frame (required for __try).
void ApplyOne(const PendingDvarWriteX64& w)
{
    char requested[128] = "";
    char readBack[160] = "";
    const char* outcome = "FAILED";
    FormatRequested(w, requested, sizeof(requested));
    __try {
        void* dvar = FindDvarX64_Exported(w.name);
        if (!dvar) {
            outcome = "SKIPPED (no such dvar)";
        } else {
            DispatchTypedWrite(dvar, w);
            FormatDvarValue(dvar, readBack, sizeof(readBack));
            const int match = ReadBackMatches(w, dvar);
            // A mismatch means Dvar_SetVariant rejected the value (outside the dvar's
            // registered domain) or its domain callback vetoed it -- the dvar keeps
            // its previous value.
            outcome = match == 1 ? "OK" : (match == 0 ? "MISMATCH (value rejected by the dvar's domain)" : "applied");
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        outcome = "FAILED (SEH exception in engine setter)";
    }
    char line[448];
    sprintf_s(line, "[x64-dvarwrite] %.63s = %s -> read-back %s : %s",
              w.name, requested, readBack[0] ? readBack : "-", outcome);
    LogFromController(line);
}

void DrainPendingDvarWrites()
{
    if (InterlockedCompareExchange(&g_pendingNonEmpty, 0, 0) == 0) return;
    int count = 0;
    AcquireSRWLockExclusive(&g_pendingLock);
    count = g_pendingCount;
    for (int i = 0; i < count; ++i) g_drainBuffer[i] = g_pending[i];
    g_pendingCount = 0;
    InterlockedExchange(&g_pendingNonEmpty, 0);
    ReleaseSRWLockExclusive(&g_pendingLock);

    static bool s_loggedFirstDrain = false;
    if (!s_loggedFirstDrain && count > 0) {
        s_loggedFirstDrain = true;
        char b[128];
        sprintf_s(b, "[x64-dvarwrite] first drain on tid=%lu (Com_Frame body, engine main thread)",
                  GetCurrentThreadId());
        LogFromController(b);
    }
    for (int i = 0; i < count; ++i) ApplyOne(g_drainBuffer[i]);
}

void Hook_ComFrameBodyX64()
{
    DrainPendingDvarWrites();
    g_origComFrameBody();
}

bool ResolveOne(const char* signature, const char* label, void** out)
{
    SigScan::Result r = SigScan::FindPatternInMainModule(signature);
    char buf[192];
    if (!r.found) {
        sprintf_s(buf, "[x64-dvarwrite] FATAL: %s signature did not resolve -- dvar writes disabled this session", label);
        LogFromController(buf);
        return false;
    }
    *out = reinterpret_cast<void*>(r.address);
    sprintf_s(buf, "[x64-dvarwrite] %s resolved @ 0x%llX", label, static_cast<unsigned long long>(r.address));
    LogFromController(buf);
    return true;
}

} // namespace

extern "C" bool QueueDvarBoolWriteX64(const char* name, bool value)
{
    if (!ValidateDvarName(name)) return false;
    PendingDvarWriteX64 e = {};
    strcpy_s(e.name, name);
    e.kind = DvarWriteKindX64::Bool;
    e.boolValue = value;
    return Enqueue(e);
}

extern "C" bool QueueDvarIntWriteX64(const char* name, int value)
{
    if (!ValidateDvarName(name)) return false;
    PendingDvarWriteX64 e = {};
    strcpy_s(e.name, name);
    e.kind = DvarWriteKindX64::Int;
    e.intValue = value;
    return Enqueue(e);
}

extern "C" bool QueueDvarFloatWriteX64(const char* name, float value)
{
    if (!ValidateDvarName(name)) return false;
    PendingDvarWriteX64 e = {};
    strcpy_s(e.name, name);
    e.kind = DvarWriteKindX64::Float;
    e.floatValue = value;
    return Enqueue(e);
}

extern "C" bool QueueDvarStringWriteX64(const char* name, const char* value)
{
    if (!ValidateDvarName(name)) return false;
    if (!value || strnlen(value, kMaxDvarStringLen) >= kMaxDvarStringLen) {
        LogOnce(&g_loggedBadString, "[x64-dvarwrite] rejected a string write: value null or longer than 255 chars");
        return false;
    }
    PendingDvarWriteX64 e = {};
    strcpy_s(e.name, name);
    e.kind = DvarWriteKindX64::String;
    strcpy_s(e.stringValue, value);
    return Enqueue(e);
}

extern "C" bool IsDvarWritePathAvailableX64()
{
    return g_dvarWriteAvailable;
}

void InstallDvarWriteX64()
{
    // Every piece is required: a write path with a missing setter or no main-thread
    // drain would accept writes it can never apply.
    bool ok = true;
    ok &= ResolveOne(kDvarSetBoolX64Signature, "Dvar_SetBool", reinterpret_cast<void**>(&g_dvarSetBool));
    ok &= ResolveOne(kDvarSetIntX64Signature, "Dvar_SetInt", reinterpret_cast<void**>(&g_dvarSetInt));
    ok &= ResolveOne(kDvarSetFloatX64Signature, "Dvar_SetFloat", reinterpret_cast<void**>(&g_dvarSetFloat));
    ok &= ResolveOne(kDvarSetFromStringX64Signature, "Dvar_SetFromStringFromSource",
                     reinterpret_cast<void**>(&g_dvarSetFromString));
    // The writes look each dvar up by name through analog_input_hooks_x64.cpp's
    // Dvar_FindVar. A signature miss there would turn every write into "no such
    // dvar", so it fails the install loudly here instead. (Checked by resolution,
    // not by probing a real dvar: this runs from DllMain, before the engine has
    // registered any.)
    if (!IsFindDvarX64Resolved()) {
        LogFromController("[x64-dvarwrite] FATAL: Dvar_FindVar did not resolve -- dvar writes disabled this session");
        ok = false;
    }
    void* comFrameBody = nullptr;
    ok &= ResolveOne(kComFrameBodyX64Signature, "Com_Frame body (main-thread drain point)", &comFrameBody);
    if (!ok) return;

    MH_STATUS createStatus = MH_CreateHook(comFrameBody, reinterpret_cast<void*>(&Hook_ComFrameBodyX64),
                                           reinterpret_cast<void**>(&g_origComFrameBody));
    if (createStatus != MH_OK) {
        char buf[160];
        sprintf_s(buf, "[x64-dvarwrite] FATAL: MH_CreateHook failed for the Com_Frame body @ 0x%p (status=%d)",
                  comFrameBody, static_cast<int>(createStatus));
        LogFromController(buf);
        return;
    }
    MH_STATUS enableStatus = MH_EnableHook(comFrameBody);
    if (enableStatus != MH_OK) {
        char buf[160];
        sprintf_s(buf, "[x64-dvarwrite] FATAL: MH_EnableHook failed for the Com_Frame body @ 0x%p (status=%d)",
                  comFrameBody, static_cast<int>(enableStatus));
        LogFromController(buf);
        MH_RemoveHook(comFrameBody);
        return;
    }
    g_dvarWriteAvailable = true;
    LogFromController("[x64-dvarwrite] dvar write path live -- writes from any thread are queued and applied on the "
        "engine main thread at the top of the next Com_Frame body");
}
