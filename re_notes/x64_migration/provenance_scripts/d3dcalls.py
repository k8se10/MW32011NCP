# usage: python3 d3dcalls.py <exe> <deviceGlobalVA> -- map IDirect3DDevice9 methods to calling functions.
# Map IDirect3DDevice9 method calls: for functions that load the device global, track vtable-offset calls.
import pefile, capstone, sys, re, collections
M = "QueryInterface AddRef Release TestCooperativeLevel GetAvailableTextureMem EvictManagedResources GetDirect3D GetDeviceCaps GetDisplayMode GetCreationParameters SetCursorProperties SetCursorPosition ShowCursor CreateAdditionalSwapChain GetSwapChain GetNumberOfSwapChains Reset Present GetBackBuffer GetRasterStatus SetDialogBoxMode SetGammaRamp GetGammaRamp CreateTexture CreateVolumeTexture CreateCubeTexture CreateVertexBuffer CreateIndexBuffer CreateRenderTarget CreateDepthStencilSurface UpdateSurface UpdateTexture GetRenderTargetData GetFrontBufferData StretchRect ColorFill CreateOffscreenPlainSurface SetRenderTarget GetRenderTarget SetDepthStencilSurface GetDepthStencilSurface BeginScene EndScene Clear SetTransform GetTransform MultiplyTransform SetViewport GetViewport SetMaterial GetMaterial SetLight GetLight LightEnable GetLightEnable SetClipPlane GetClipPlane SetRenderState GetRenderState CreateStateBlock BeginStateBlock EndStateBlock SetClipStatus GetClipStatus GetTexture SetTexture GetTextureStageState SetTextureStageState GetSamplerState SetSamplerState ValidateDevice SetPaletteEntries GetPaletteEntries SetCurrentTexturePalette GetCurrentTexturePalette SetScissorRect GetScissorRect SetSoftwareVertexProcessing GetSoftwareVertexProcessing SetNPatchMode GetNPatchMode DrawPrimitive DrawIndexedPrimitive DrawPrimitiveUP DrawIndexedPrimitiveUP ProcessVertices CreateVertexDeclaration SetVertexDeclaration GetVertexDeclaration SetFVF GetFVF CreateVertexShader SetVertexShader GetVertexShader SetVertexShaderConstantF GetVertexShaderConstantF SetVertexShaderConstantI GetVertexShaderConstantI SetVertexShaderConstantB GetVertexShaderConstantB SetStreamSource GetStreamSource SetStreamSourceFreq GetStreamSourceFreq SetIndices GetIndices CreatePixelShader SetPixelShader GetPixelShader SetPixelShaderConstantF GetPixelShaderConstantF SetPixelShaderConstantI GetPixelShaderConstantI SetPixelShaderConstantB GetPixelShaderConstantB DrawRectPatch DrawTriPatch DeletePatch CreateQuery".split()
DEV = int(sys.argv[2], 16)
pe = pefile.PE(sys.argv[1]); b = pe.OPTIONAL_HEADER.ImageBase; img = pe.get_memory_mapped_image()
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64); md.detail = True
use = collections.defaultdict(set); byfn = collections.defaultdict(set)
for e in pe.DIRECTORY_ENTRY_EXCEPTION:
    s, t = e.struct.BeginAddress, e.struct.EndAddress
    ins = list(md.disasm(img[s:t], b + s))
    devregs = {}; vtregs = {}
    for i in ins:
        ops = i.operands
        # reg <- [rip+DEV]
        if i.mnemonic == 'mov' and len(ops) == 2 and ops[0].type == capstone.x86.X86_OP_REG and ops[1].type == capstone.x86.X86_OP_MEM and ops[1].mem.base == capstone.x86.X86_REG_RIP and i.address + i.size + ops[1].mem.disp == DEV:
            devregs[ops[0].reg] = i.address; continue
        # vt <- [devreg]
        if i.mnemonic == 'mov' and len(ops) == 2 and ops[0].type == capstone.x86.X86_OP_REG and ops[1].type == capstone.x86.X86_OP_MEM and ops[1].mem.base in devregs and ops[1].mem.disp == 0 and ops[1].mem.index == 0:
            vtregs[ops[0].reg] = i.address; continue
        if i.mnemonic in ('call', 'jmp') and ops and ops[0].type == capstone.x86.X86_OP_MEM and ops[0].mem.base in vtregs and ops[0].mem.index == 0:
            off = ops[0].mem.disp
            if off % 8 == 0 and off // 8 < len(M):
                use[M[off // 8]].add(b + s); byfn[b + s].add(M[off // 8])
        # invalidate regs overwritten
        if ops and ops[0].type == capstone.x86.X86_OP_REG and i.mnemonic not in ('cmp', 'test', 'push'):
            r = ops[0].reg
            if r in devregs and not (i.mnemonic == 'mov' and ops[1].type == capstone.x86.X86_OP_MEM and ops[1].mem.base == capstone.x86.X86_REG_RIP): devregs.pop(r, None)
            if r in vtregs and i.mnemonic != 'mov': vtregs.pop(r, None)
for m in M:
    if m in use: print('%-26s %3d fns: %s' % (m, len(use[m]), ' '.join(hex(x) for x in sorted(use[m])[:12])))
