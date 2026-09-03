; forward_stubs_x64.asm -- x64 replacement for dllmain.cpp's x86-only FORWARD_STUB
; macro (__declspec(naked) + inline __asm { jmp dword ptr [...] }), neither of which
; MSVC's x64 compiler supports at all (hard compiler limitations, not project bugs --
; confirmed 2026-09-03 while bringing up the first x64 build, see
; re_notes/x64_migration/README.md, issue #111).
;
; Same technique, same semantics, just expressed as real MASM (assembled by ml64.exe,
; wired into the Win32 config only -- see the x64-only ItemGroup in proxy_d3d9.vcxproj):
; a pure tail-jump through the resolved real-export pointer. A JMP (not CALL+RET)
; preserves the original caller's register/stack state exactly as the x64 calling
; convention already has it, so the real system d3d9.dll's own function returns
; directly to MW3's own call site -- same "works regardless of the real export's
; exact argument count" property the x86 version relies on. Each g_real_<name> global
; is defined in dllmain.cpp (a plain `void*`, resolved by ResolveRealExports() via
; GetProcAddress) and referenced here via EXTERN.
;
; x64 has no leading-underscore C-name decoration (unlike x86 cdecl/stdcall), so these
; bare names already match both the C++ global names and proxy_d3d9.def's EXPORTS list
; without any extra mangling to account for.

EXTERN g_real_D3DPERF_BeginEvent:QWORD
EXTERN g_real_D3DPERF_EndEvent:QWORD
EXTERN g_real_D3DPERF_GetStatus:QWORD
EXTERN g_real_D3DPERF_QueryRepeatFrame:QWORD
EXTERN g_real_D3DPERF_SetMarker:QWORD
EXTERN g_real_D3DPERF_SetOptions:QWORD
EXTERN g_real_D3DPERF_SetRegion:QWORD
EXTERN g_real_DebugSetLevel:QWORD
EXTERN g_real_DebugSetMute:QWORD
EXTERN g_real_Direct3D9EnableMaximizedWindowedModeShim:QWORD
EXTERN g_real_Direct3DCreate9Ex:QWORD
EXTERN g_real_Direct3DCreate9On12:QWORD
EXTERN g_real_Direct3DCreate9On12Ex:QWORD
EXTERN g_real_Direct3DShaderValidatorCreate9:QWORD
EXTERN g_real_PSGPError:QWORD
EXTERN g_real_PSGPSampleTexture:QWORD

.code

D3DPERF_BeginEvent PROC
    jmp QWORD PTR [g_real_D3DPERF_BeginEvent]
D3DPERF_BeginEvent ENDP

D3DPERF_EndEvent PROC
    jmp QWORD PTR [g_real_D3DPERF_EndEvent]
D3DPERF_EndEvent ENDP

D3DPERF_GetStatus PROC
    jmp QWORD PTR [g_real_D3DPERF_GetStatus]
D3DPERF_GetStatus ENDP

D3DPERF_QueryRepeatFrame PROC
    jmp QWORD PTR [g_real_D3DPERF_QueryRepeatFrame]
D3DPERF_QueryRepeatFrame ENDP

D3DPERF_SetMarker PROC
    jmp QWORD PTR [g_real_D3DPERF_SetMarker]
D3DPERF_SetMarker ENDP

D3DPERF_SetOptions PROC
    jmp QWORD PTR [g_real_D3DPERF_SetOptions]
D3DPERF_SetOptions ENDP

D3DPERF_SetRegion PROC
    jmp QWORD PTR [g_real_D3DPERF_SetRegion]
D3DPERF_SetRegion ENDP

DebugSetLevel PROC
    jmp QWORD PTR [g_real_DebugSetLevel]
DebugSetLevel ENDP

DebugSetMute PROC
    jmp QWORD PTR [g_real_DebugSetMute]
DebugSetMute ENDP

Direct3D9EnableMaximizedWindowedModeShim PROC
    jmp QWORD PTR [g_real_Direct3D9EnableMaximizedWindowedModeShim]
Direct3D9EnableMaximizedWindowedModeShim ENDP

Direct3DCreate9Ex PROC
    jmp QWORD PTR [g_real_Direct3DCreate9Ex]
Direct3DCreate9Ex ENDP

Direct3DCreate9On12 PROC
    jmp QWORD PTR [g_real_Direct3DCreate9On12]
Direct3DCreate9On12 ENDP

Direct3DCreate9On12Ex PROC
    jmp QWORD PTR [g_real_Direct3DCreate9On12Ex]
Direct3DCreate9On12Ex ENDP

Direct3DShaderValidatorCreate9 PROC
    jmp QWORD PTR [g_real_Direct3DShaderValidatorCreate9]
Direct3DShaderValidatorCreate9 ENDP

PSGPError PROC
    jmp QWORD PTR [g_real_PSGPError]
PSGPError ENDP

PSGPSampleTexture PROC
    jmp QWORD PTR [g_real_PSGPSampleTexture]
PSGPSampleTexture ENDP

END
