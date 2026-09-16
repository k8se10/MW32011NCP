// iwd_read_cache_x64.cpp -- persistent memory-mapped .iwd archive read
// cache, x64-only.
//
// ATTRIBUTION: the persistent-mapping cache design and its data structures
// below (FilePathSlot/IwdArchiveMapping/IwdHandleSlot, IsIwdPath,
// LockOrCreateHandle, TryServeIwdRead) are ported, with real credit, from
// legoliamneeson/MW3_Standalone_D3D9_Project (github.com/legoliamneeson/
// MW3_Standalone_D3D9_Project, src/runtime.hpp) -- a real external reference
// implementation this project independently evaluated (the "D3D9 optimizer
// repo evaluation" round, 2026-09-16, continued per direct instruction: "we
// still need to do the other fixes from that too"). That project's own
// README states no redistribution license is asserted for this specific
// runtime source; credited here, in README.md's Credits section, and via a
// Co-Authored-By line on the commit that lands it.
//
// WHY THE SOURCE PROJECT'S RVAs ARE SAFE LEADS HERE: a dedicated research
// fork this session confirmed, via direct raw-byte comparison against our
// OWN live iw5sp.exe (not assumed), that every one of the source project's
// real code signatures for this technique -- MW3_CRT_READ_SIGNATURE,
// MW3_IWD_READ_CALL_SIGNATURE, MW3_MINIZIP_INFLATE_CALL_SIGNATURE,
// MW3_ZLIB_INFLATE_END_SIGNATURE, MW3_FS_SEEK_SIGNATURE -- match our binary
// byte-for-byte at the identical address (our own SHA256 differs from the
// source project's stated target, almost certainly an Authenticode
// certificate difference, not a code difference -- see
// wait_coalescing_x64.cpp's own header comment for the same finding applied
// to its own signatures).
//
// WHAT'S DELIBERATELY *NOT* PORTED, AND WHY: the source project's OWN
// implementation also detours the raw CRT `_read` entry point directly and
// pokes the CRT's own internal static file-descriptor table (fd >> 6
// indexing, a hardcoded 72-byte record layout, raw offsets into Microsoft's
// own undocumented CRT internals) for an even lower-level fast path. That
// specific piece could NOT be independently verified the way the pure-code
// signatures above were -- the research fork's own report flagged it
// explicitly: the CRT FD table is runtime-populated (all-zero in the static
// file image), so static byte comparison can't confirm its layout, and
// getting an internal CRT struct offset wrong risks real memory corruption,
// not just a missed optimization. This file ONLY hooks the real, documented
// Win32 API layer (CreateFileA/W, ReadFile, SetFilePointer(Ex), CloseHandle)
// -- the exact same real-bytes-verified `MW3_IWD_READ_CALL_SIGNATURE` call
// site the source project's own `TryServeIwdRead` is *also* invoked from
// (via its own `HookReadFile`, not exclusively the CRT detour) -- so the
// actual caching behavior for the game's real .iwd streaming reads is
// unchanged; only the riskier, unverifiable low-level duplicate path is
// omitted. A future session could add the CRT-level path IF it first
// independently verifies the FD table layout via a real self-dump read
// (this project's own proven-safe `TriggerSelfMemoryDumpX64` technique) --
// not attempted this pass.

#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <array>

#include "../third_party/minhook/include/MinHook.h"
#include "signature_scan.h"
#include "mod_config.h"

extern void LogFromController(const char* msg);
void NotifyArchiveIoActivityX64(unsigned int bytes); // wait_coalescing_x64.h --
    // shares the same burst-detection/priority-boost state this cache's own
    // real archive reads should feed, per the source project's own design.

namespace {

constexpr size_t kFilePathSlotCount = 256;
constexpr size_t kIwdHandleSlotCount = 64;
constexpr size_t kIwdArchiveSlotCount = 96;
constexpr size_t kPathBufSize = 160;

// Real byte pattern for the .iwd streaming loader's own `call [ReadFile]`
// site -- ported verbatim from MW3_IWD_READ_CALL_SIGNATURE (runtime.hpp),
// independently byte-confirmed against our own live binary this session.
// Not wildcarded (a documented exception, same reasoning as
// wait_coalescing_x64.cpp's Sleep(1) patterns): a `call [ReadFile]` IAT
// thunk shape recurs throughout the whole binary, so wildcarding the IAT
// displacement would make this pattern match dozens of unrelated ReadFile
// call sites instead of the one specific .iwd loader instruction; the
// literal bytes are what make it uniquely resolvable. A future binary
// update moving this exact call simply fails the scan gracefully (the
// cache never activates) rather than crashing.
constexpr char kIwdReadCallSignature[] = "FF 15 BD 7C 02 00";

uintptr_t g_iwdStreamReadCallerAddr = 0;

struct FilePathSlot {
    uintptr_t handle = 0;
    std::array<char, kPathBufSize> path{};
};
FilePathSlot g_filePaths[kFilePathSlotCount];
SRWLOCK g_filePathLock = SRWLOCK_INIT;

size_t FilePathSlotIndex(uintptr_t handle)
{
    handle ^= handle >> 11;
    handle ^= handle >> 23;
    return static_cast<size_t>(handle % kFilePathSlotCount);
}

void StoreFilePathA(HANDLE handle, const char* path)
{
    if (!handle || handle == INVALID_HANDLE_VALUE || !path || path[0] == '\0') return;
    const uintptr_t key = reinterpret_cast<uintptr_t>(handle);
    AcquireSRWLockExclusive(&g_filePathLock);
    FilePathSlot& slot = g_filePaths[FilePathSlotIndex(key)];
    slot.handle = key;
    slot.path.fill('\0');
    strncpy_s(slot.path.data(), slot.path.size(), path, _TRUNCATE);
    ReleaseSRWLockExclusive(&g_filePathLock);
}

void StoreFilePathW(HANDLE handle, const wchar_t* path)
{
    if (!handle || handle == INVALID_HANDLE_VALUE || !path || path[0] == L'\0') return;
    std::array<char, kPathBufSize> converted{};
    const int result = WideCharToMultiByte(CP_UTF8, 0, path, -1,
        converted.data(), static_cast<int>(converted.size()), nullptr, nullptr);
    if (result > 0) StoreFilePathA(handle, converted.data());
}

void RemoveFilePathForHandle(HANDLE handle)
{
    if (!handle || handle == INVALID_HANDLE_VALUE) return;
    const uintptr_t key = reinterpret_cast<uintptr_t>(handle);
    AcquireSRWLockExclusive(&g_filePathLock);
    FilePathSlot& slot = g_filePaths[FilePathSlotIndex(key)];
    if (slot.handle == key) {
        slot.handle = 0;
        slot.path.fill('\0');
    }
    ReleaseSRWLockExclusive(&g_filePathLock);
}

void CopyFilePathForHandle(uintptr_t handle, std::array<char, kPathBufSize>& out)
{
    out.fill('\0');
    if (!handle || handle == reinterpret_cast<uintptr_t>(INVALID_HANDLE_VALUE)) return;
    AcquireSRWLockShared(&g_filePathLock);
    const FilePathSlot& slot = g_filePaths[FilePathSlotIndex(handle)];
    if (slot.handle == handle) out = slot.path;
    ReleaseSRWLockShared(&g_filePathLock);
}

bool ResolveFilePathFromLiveHandle(uintptr_t handle, std::array<char, kPathBufSize>& out)
{
    if (out[0] != '\0' || !handle || handle == reinterpret_cast<uintptr_t>(INVALID_HANDLE_VALUE)) {
        return out[0] != '\0';
    }
    wchar_t widePath[512]{};
    const DWORD count = GetFinalPathNameByHandleW(reinterpret_cast<HANDLE>(handle),
        widePath, static_cast<DWORD>(sizeof(widePath) / sizeof(widePath[0])), FILE_NAME_NORMALIZED);
    if (count == 0 || count >= (sizeof(widePath) / sizeof(widePath[0]))) return false;

    const wchar_t* source = widePath;
    if (count >= 4 && widePath[0] == L'\\' && widePath[1] == L'\\' && widePath[2] == L'?' && widePath[3] == L'\\') {
        source += 4;
    }
    const int converted = WideCharToMultiByte(CP_UTF8, 0, source, -1,
        out.data(), static_cast<int>(out.size()), nullptr, nullptr);
    if (converted <= 0) {
        out.fill('\0');
        return false;
    }
    StoreFilePathA(reinterpret_cast<HANDLE>(handle), out.data());
    return true;
}

bool IsIwdPath(const std::array<char, kPathBufSize>& path)
{
    size_t length = 0;
    while (length < path.size() && path[length] != '\0') ++length;
    if (length < 4) return false;
    auto lowerAscii = [](char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + ('a' - 'A')) : c; };
    return lowerAscii(path[length - 4]) == '.'
        && lowerAscii(path[length - 3]) == 'i'
        && lowerAscii(path[length - 2]) == 'w'
        && lowerAscii(path[length - 1]) == 'd';
}

bool IwdPathEquals(const std::array<char, kPathBufSize>& a, const std::array<char, kPathBufSize>& b)
{
    for (size_t i = 0; i < a.size(); ++i) {
        char ca = a[i], cb = b[i];
        if (ca >= 'A' && ca <= 'Z') ca = static_cast<char>(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = static_cast<char>(cb - 'A' + 'a');
        if (ca != cb) return false;
        if (ca == '\0') return true;
    }
    return true;
}

struct IwdArchiveMapping {
    HANDLE mapping = nullptr;
    const uint8_t* view = nullptr;
    uint64_t size = 0;
    std::array<char, kPathBufSize> path{};
};
struct IwdHandleSlot {
    SRWLOCK lock = SRWLOCK_INIT;
    uintptr_t handle = 0;
    const uint8_t* view = nullptr;
    uint64_t size = 0;
    uint64_t virtualPosition = 0;
    bool positionValid = false;
    uint16_t archiveIndex = 0xFFFF;
};
IwdArchiveMapping g_archives[kIwdArchiveSlotCount];
IwdHandleSlot g_handles[kIwdHandleSlotCount];
SRWLOCK g_iwdTableLock = SRWLOCK_INIT;

thread_local IwdHandleSlot* g_tlsHandleSlot = nullptr;
thread_local uintptr_t g_tlsHandle = 0;

IwdHandleSlot* FindHandleNoLock(uintptr_t handle)
{
    if (!handle || handle == reinterpret_cast<uintptr_t>(INVALID_HANDLE_VALUE)) return nullptr;
    for (auto& slot : g_handles) if (slot.handle == handle) return &slot;
    return nullptr;
}
IwdHandleSlot* FindEmptyHandleNoLock()
{
    for (auto& slot : g_handles) if (slot.handle == 0) return &slot;
    return nullptr;
}
int FindArchiveNoLock(const std::array<char, kPathBufSize>& path)
{
    for (size_t i = 0; i < kIwdArchiveSlotCount; ++i) {
        if (g_archives[i].view != nullptr && IwdPathEquals(g_archives[i].path, path)) return static_cast<int>(i);
    }
    return -1;
}
int FindEmptyArchiveNoLock()
{
    for (size_t i = 0; i < kIwdArchiveSlotCount; ++i) if (g_archives[i].view == nullptr) return static_cast<int>(i);
    return -1;
}
void ResetHandleSlotLocked(IwdHandleSlot& slot)
{
    slot.handle = 0;
    slot.view = nullptr;
    slot.size = 0;
    slot.virtualPosition = 0;
    slot.positionValid = false;
    slot.archiveIndex = 0xFFFF;
}

IwdHandleSlot* LockExistingHandle(HANDLE file)
{
    const uintptr_t key = reinterpret_cast<uintptr_t>(file);
    if (g_tlsHandleSlot != nullptr && g_tlsHandle == key) {
        AcquireSRWLockExclusive(&g_tlsHandleSlot->lock);
        if (g_tlsHandleSlot->handle == key && g_tlsHandleSlot->view != nullptr) {
            return g_tlsHandleSlot;
        }
        ReleaseSRWLockExclusive(&g_tlsHandleSlot->lock);
        g_tlsHandleSlot = nullptr;
        g_tlsHandle = 0;
    }
    AcquireSRWLockShared(&g_iwdTableLock);
    IwdHandleSlot* slot = FindHandleNoLock(key);
    if (slot != nullptr) AcquireSRWLockExclusive(&slot->lock);
    ReleaseSRWLockShared(&g_iwdTableLock);
    if (slot != nullptr && slot->handle == key && slot->view != nullptr) {
        g_tlsHandleSlot = slot;
        g_tlsHandle = key;
    }
    return slot;
}
void UnlockHandle(IwdHandleSlot* slot)
{
    if (slot != nullptr) ReleaseSRWLockExclusive(&slot->lock);
}
bool SyncKernelPointerLocked(HANDLE file, IwdHandleSlot& slot)
{
    if (!slot.positionValid) return true;
    LARGE_INTEGER position{};
    position.QuadPart = static_cast<LONGLONG>(slot.virtualPosition);
    return SetFilePointerEx(file, position, nullptr, FILE_BEGIN) != FALSE;
}

IwdHandleSlot* LockOrCreateHandle(HANDLE file)
{
    if (!g_modConfig.iwdReadAccelEnabled || !file || file == INVALID_HANDLE_VALUE) return nullptr;
    if (IwdHandleSlot* existing = LockExistingHandle(file)) return existing;

    std::array<char, kPathBufSize> path{};
    CopyFilePathForHandle(reinterpret_cast<uintptr_t>(file), path);
    ResolveFilePathFromLiveHandle(reinterpret_cast<uintptr_t>(file), path);
    if (!IsIwdPath(path)) return nullptr;

    AcquireSRWLockExclusive(&g_iwdTableLock);
    const uintptr_t key = reinterpret_cast<uintptr_t>(file);
    IwdHandleSlot* slot = FindHandleNoLock(key);
    if (slot == nullptr) slot = FindEmptyHandleNoLock();
    if (slot == nullptr) {
        ReleaseSRWLockExclusive(&g_iwdTableLock);
        return nullptr;
    }

    AcquireSRWLockExclusive(&slot->lock);
    if (slot->handle == key && slot->view != nullptr) {
        g_tlsHandleSlot = slot;
        g_tlsHandle = key;
        ReleaseSRWLockExclusive(&g_iwdTableLock);
        return slot;
    }

    int archiveIndex = FindArchiveNoLock(path);
    if (archiveIndex < 0) {
        archiveIndex = FindEmptyArchiveNoLock();
        if (archiveIndex < 0) {
            ReleaseSRWLockExclusive(&slot->lock);
            ReleaseSRWLockExclusive(&g_iwdTableLock);
            return nullptr;
        }

        LARGE_INTEGER size{};
        HANDLE mapping = nullptr;
        const uint8_t* view = nullptr;
        if (GetFileSizeEx(file, &size) != FALSE && size.QuadPart > 0) {
            mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
            if (mapping != nullptr) {
                view = static_cast<const uint8_t*>(MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0));
            }
        }
        if (view == nullptr) {
            if (mapping != nullptr) CloseHandle(mapping);
            ReleaseSRWLockExclusive(&slot->lock);
            ReleaseSRWLockExclusive(&g_iwdTableLock);
            return nullptr;
        }

        IwdArchiveMapping& archive = g_archives[static_cast<size_t>(archiveIndex)];
        archive.mapping = mapping;
        archive.view = view;
        archive.size = static_cast<uint64_t>(size.QuadPart);
        archive.path = path;

        char buf[224];
        sprintf_s(buf, "[iwd-cache] persistent archive view: %s | %.1f MiB",
            archive.path.data(), static_cast<double>(archive.size) / (1024.0 * 1024.0));
        LogFromController(buf);
    }

    IwdArchiveMapping& archive = g_archives[static_cast<size_t>(archiveIndex)];
    LARGE_INTEGER zero{};
    LARGE_INTEGER current{};
    const BOOL havePosition = SetFilePointerEx(file, zero, &current, FILE_CURRENT);

    slot->handle = key;
    slot->view = archive.view;
    slot->size = archive.size;
    slot->virtualPosition = (havePosition != FALSE && current.QuadPart >= 0) ? static_cast<uint64_t>(current.QuadPart) : 0;
    slot->positionValid = havePosition != FALSE && current.QuadPart >= 0;
    slot->archiveIndex = static_cast<uint16_t>(archiveIndex);

    g_tlsHandleSlot = slot;
    g_tlsHandle = key;
    ReleaseSRWLockExclusive(&g_iwdTableLock);
    return slot;
}

bool TryServeIwdRead(HANDLE file, LPVOID buffer, DWORD bytesToRead, LPDWORD bytesRead,
    LPOVERLAPPED overlapped, uintptr_t callerAddr, DWORD& actualOut)
{
    actualOut = 0;
    if (!g_modConfig.iwdReadAccelEnabled
        || callerAddr != g_iwdStreamReadCallerAddr
        || g_iwdStreamReadCallerAddr == 0
        || overlapped != nullptr
        || buffer == nullptr) {
        return false;
    }

    IwdHandleSlot* slot = LockOrCreateHandle(file);
    if (slot == nullptr) return false;

    if (!slot->positionValid || slot->virtualPosition > slot->size) {
        UnlockHandle(slot);
        return false;
    }

    const uint64_t available = slot->size - slot->virtualPosition;
    const uint64_t requested = static_cast<uint64_t>(bytesToRead);
    const DWORD actual = static_cast<DWORD>(available < requested ? available : requested);

    if (actual != 0) {
        memcpy(buffer, slot->view + slot->virtualPosition, static_cast<size_t>(actual));
    }
    slot->virtualPosition += actual;
    actualOut = actual;
    if (bytesRead != nullptr) *bytesRead = actual;
    UnlockHandle(slot);

    NotifyArchiveIoActivityX64(actual);
    return true;
}

void RemoveHandleBinding(HANDLE handle)
{
    if (!handle || handle == INVALID_HANDLE_VALUE) return;
    const uintptr_t key = reinterpret_cast<uintptr_t>(handle);
    if (g_tlsHandle == key) {
        g_tlsHandleSlot = nullptr;
        g_tlsHandle = 0;
    }
    AcquireSRWLockExclusive(&g_iwdTableLock);
    IwdHandleSlot* slot = FindHandleNoLock(key);
    if (slot != nullptr) {
        AcquireSRWLockExclusive(&slot->lock);
        ResetHandleSlotLocked(*slot);
        ReleaseSRWLockExclusive(&slot->lock);
    }
    ReleaseSRWLockExclusive(&g_iwdTableLock);
    RemoveFilePathForHandle(handle);
}

// ---- Win32 hook plumbing ----------------------------------------------------

using ReadFileFn = BOOL(WINAPI*)(HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);
using CreateFileAFn = HANDLE(WINAPI*)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
using CreateFileWFn = HANDLE(WINAPI*)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
using CloseHandleFn = BOOL(WINAPI*)(HANDLE);
using SetFilePointerFn = DWORD(WINAPI*)(HANDLE, LONG, PLONG, DWORD);
using SetFilePointerExFn = BOOL(WINAPI*)(HANDLE, LARGE_INTEGER, PLARGE_INTEGER, DWORD);

ReadFileFn g_realReadFile = nullptr;
CreateFileAFn g_realCreateFileA = nullptr;
CreateFileWFn g_realCreateFileW = nullptr;
CloseHandleFn g_realCloseHandle = nullptr;
SetFilePointerFn g_realSetFilePointer = nullptr;
SetFilePointerExFn g_realSetFilePointerEx = nullptr;

BOOL WINAPI Hook_ReadFile(HANDLE file, LPVOID buffer, DWORD bytesToRead, LPDWORD bytesRead, LPOVERLAPPED overlapped)
{
    if (g_realReadFile == nullptr) return FALSE;
    const uintptr_t callerAddr = reinterpret_cast<uintptr_t>(_ReturnAddress());
    const DWORD incomingLastError = GetLastError();

    DWORD localBytesRead = 0;
    LPDWORD observedBytesRead = bytesRead;
    if (observedBytesRead == nullptr && overlapped == nullptr) observedBytesRead = &localBytesRead;

    DWORD cachedActual = 0;
    if (TryServeIwdRead(file, buffer, bytesToRead, observedBytesRead, overlapped, callerAddr, cachedActual)) {
        SetLastError(incomingLastError);
        return TRUE;
    }

    IwdHandleSlot* slot = (overlapped == nullptr) ? LockExistingHandle(file) : nullptr;
    if (slot != nullptr && !SyncKernelPointerLocked(file, *slot)) {
        UnlockHandle(slot);
        slot = nullptr;
    }

    SetLastError(incomingLastError);
    const BOOL result = g_realReadFile(file, buffer, bytesToRead, observedBytesRead, overlapped);
    const DWORD actual = (result != FALSE && observedBytesRead != nullptr) ? *observedBytesRead : 0;

    if (slot != nullptr) {
        if (result != FALSE && slot->positionValid) slot->virtualPosition += actual;
        UnlockHandle(slot);
    }
    return result;
}

HANDLE WINAPI Hook_CreateFileA(LPCSTR filename, DWORD desiredAccess, DWORD shareMode,
    LPSECURITY_ATTRIBUTES security, DWORD creationDisposition, DWORD flagsAndAttributes, HANDLE templateFile)
{
    if (g_realCreateFileA == nullptr) return INVALID_HANDLE_VALUE;
    const HANDLE result = g_realCreateFileA(filename, desiredAccess, shareMode, security,
        creationDisposition, flagsAndAttributes, templateFile);
    StoreFilePathA(result, filename);
    return result;
}

HANDLE WINAPI Hook_CreateFileW(LPCWSTR filename, DWORD desiredAccess, DWORD shareMode,
    LPSECURITY_ATTRIBUTES security, DWORD creationDisposition, DWORD flagsAndAttributes, HANDLE templateFile)
{
    if (g_realCreateFileW == nullptr) return INVALID_HANDLE_VALUE;
    const HANDLE result = g_realCreateFileW(filename, desiredAccess, shareMode, security,
        creationDisposition, flagsAndAttributes, templateFile);
    StoreFilePathW(result, filename);
    return result;
}

BOOL WINAPI Hook_CloseHandle(HANDLE handle)
{
    RemoveHandleBinding(handle);
    if (g_realCloseHandle == nullptr) return FALSE;
    return g_realCloseHandle(handle);
}

DWORD WINAPI Hook_SetFilePointer(HANDLE file, LONG distanceLow, PLONG distanceHigh, DWORD moveMethod)
{
    if (g_realSetFilePointer == nullptr) return INVALID_SET_FILE_POINTER;
    IwdHandleSlot* slot = LockExistingHandle(file);
    if (slot != nullptr) SyncKernelPointerLocked(file, *slot);

    SetLastError(NO_ERROR);
    const DWORD result = g_realSetFilePointer(file, distanceLow, distanceHigh, moveMethod);
    const DWORD savedError = GetLastError();
    const bool success = (result != INVALID_SET_FILE_POINTER) || (savedError == NO_ERROR);

    if (slot != nullptr) {
        if (success) {
            LARGE_INTEGER position{};
            position.LowPart = result;
            position.HighPart = (distanceHigh != nullptr) ? *distanceHigh : 0;
            slot->virtualPosition = (position.QuadPart >= 0) ? static_cast<uint64_t>(position.QuadPart) : 0;
            slot->positionValid = position.QuadPart >= 0;
        }
        UnlockHandle(slot);
    }
    SetLastError(savedError);
    return result;
}

BOOL WINAPI Hook_SetFilePointerEx(HANDLE file, LARGE_INTEGER distance, PLARGE_INTEGER newPosition, DWORD moveMethod)
{
    if (g_realSetFilePointerEx == nullptr) return FALSE;
    IwdHandleSlot* slot = LockExistingHandle(file);
    if (slot != nullptr) SyncKernelPointerLocked(file, *slot);

    LARGE_INTEGER localNewPosition{};
    PLARGE_INTEGER observed = (newPosition != nullptr) ? newPosition : &localNewPosition;
    const BOOL result = g_realSetFilePointerEx(file, distance, observed, moveMethod);

    if (slot != nullptr) {
        if (result != FALSE && observed->QuadPart >= 0) {
            slot->virtualPosition = static_cast<uint64_t>(observed->QuadPart);
            slot->positionValid = true;
        }
        UnlockHandle(slot);
    }
    return result;
}

} // namespace

void InstallIwdReadCacheHooksX64()
{
    SigScan::Result r = SigScan::FindPatternInMainModule(kIwdReadCallSignature);
    if (!r.found) {
        LogFromController("[iwd-cache] .iwd stream-read call-site signature did not resolve -- "
            "cache stays off, real disk I/O unaffected");
        return;
    }
    // Real byte count of the pattern above (6 tokens) -- the return address
    // a CALL from inside this pattern would produce, computed from the
    // match's own real length, never a hardcoded RVA.
    g_iwdStreamReadCallerAddr = r.address + 6;
    char buf[192];
    sprintf_s(buf, "[iwd-cache] .iwd stream-read call site resolved @ 0x%llX (caller addr 0x%llX)",
        static_cast<unsigned long long>(r.address), static_cast<unsigned long long>(g_iwdStreamReadCallerAddr));
    LogFromController(buf);

    HMODULE kernel32 = GetModuleHandleA("kernel32.dll");
    if (kernel32 == nullptr) {
        LogFromController("[iwd-cache] FATAL: GetModuleHandleA(kernel32.dll) failed");
        return;
    }

    struct HookSpec {
        const char* name;
        void* detour;
        void** original;
    };
    void* realReadFile = reinterpret_cast<void*>(GetProcAddress(kernel32, "ReadFile"));
    void* realCreateFileA = reinterpret_cast<void*>(GetProcAddress(kernel32, "CreateFileA"));
    void* realCreateFileW = reinterpret_cast<void*>(GetProcAddress(kernel32, "CreateFileW"));
    void* realCloseHandle = reinterpret_cast<void*>(GetProcAddress(kernel32, "CloseHandle"));
    void* realSetFilePointer = reinterpret_cast<void*>(GetProcAddress(kernel32, "SetFilePointer"));
    void* realSetFilePointerEx = reinterpret_cast<void*>(GetProcAddress(kernel32, "SetFilePointerEx"));

    HookSpec specs[] = {
        { "ReadFile", reinterpret_cast<void*>(&Hook_ReadFile), reinterpret_cast<void**>(&g_realReadFile) },
        { "CreateFileA", reinterpret_cast<void*>(&Hook_CreateFileA), reinterpret_cast<void**>(&g_realCreateFileA) },
        { "CreateFileW", reinterpret_cast<void*>(&Hook_CreateFileW), reinterpret_cast<void**>(&g_realCreateFileW) },
        { "CloseHandle", reinterpret_cast<void*>(&Hook_CloseHandle), reinterpret_cast<void**>(&g_realCloseHandle) },
        { "SetFilePointer", reinterpret_cast<void*>(&Hook_SetFilePointer), reinterpret_cast<void**>(&g_realSetFilePointer) },
        { "SetFilePointerEx", reinterpret_cast<void*>(&Hook_SetFilePointerEx), reinterpret_cast<void**>(&g_realSetFilePointerEx) },
    };
    void* realTargets[] = { realReadFile, realCreateFileA, realCreateFileW, realCloseHandle, realSetFilePointer, realSetFilePointerEx };

    for (size_t i = 0; i < sizeof(specs) / sizeof(specs[0]); ++i) {
        if (realTargets[i] == nullptr) {
            char errBuf[128];
            sprintf_s(errBuf, "[iwd-cache] GetProcAddress(kernel32, %s) failed -- that hook skipped", specs[i].name);
            LogFromController(errBuf);
            continue;
        }
        MH_STATUS createStatus = MH_CreateHook(realTargets[i], specs[i].detour, specs[i].original);
        MH_STATUS enableStatus = (createStatus == MH_OK) ? MH_EnableHook(realTargets[i]) : createStatus;
        char statusBuf[160];
        sprintf_s(statusBuf, "[iwd-cache] %s hook create=%d enable=%d", specs[i].name,
            static_cast<int>(createStatus), static_cast<int>(enableStatus));
        LogFromController(statusBuf);
    }
}
