// ScanVtableOffsetLoads.java -- raw-byte-scans every executable block for
// "MOV reg32,dword ptr [reg2+disp32]" (0x8B /r, ModRM mod=10, any reg/rm) where
// disp32 equals a given vtable slot byte offset -- i.e. finds every site that LOADS
// a given vtable slot's function pointer into a register, regardless of which
// register holds the interface pointer or receives the loaded pointer.
//
// Companion to ScanVtableOffsetCalls.java (which finds the literal "CALL
// [reg+disp32]" form) -- ground-truth-checked 2026-09-24 against a known-real x86
// BeginScene call site (FUN_004c0950 @ old_x86 iw5sp.exe) and found MSVC's actual
// x86 codegen here is TWO instructions, not one: "MOV EDX,[ECX+0xA4]" then a bare
// "CALL EDX" -- ScanVtableOffsetCalls's own FF/2-disp32 pattern legitimately finds
// zero matches for this compiler's output, a scanner limitation, not a true
// negative. Use THIS script for x86-era binaries; ScanVtableOffsetCalls already
// proved correct for the x64 build's own codegen (found CreateRenderTarget's one
// real call site there via the direct CALL-form pattern).
//
// Usage: -postScript ScanVtableOffsetLoads.java <output_path> <disp32_hex> [disp32_hex...]
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;

import java.io.FileWriter;
import java.io.PrintWriter;

public class ScanVtableOffsetLoads extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args == null || args.length < 2) {
            println("Usage: ScanVtableOffsetLoads.java <output_path> <disp32_hex> [disp32_hex...]");
            return;
        }
        String outPath = args[0];
        Memory mem = currentProgram.getMemory();
        FunctionManager fm = currentProgram.getFunctionManager();

        try (PrintWriter w = new PrintWriter(new FileWriter(outPath))) {
            for (int a = 1; a < args.length; a++) {
                long disp32 = Long.decode(args[a]);
                w.println("Searching for MOV reg,[reg2+0x" + Long.toHexString(disp32) + "] (8B /r disp32 form)");
                int totalMatches = 0;
                for (MemoryBlock block : mem.getBlocks()) {
                    if (!block.isExecute()) continue;
                    long size = block.getSize();
                    byte[] buf = new byte[(int) Math.min(size, Integer.MAX_VALUE - 16)];
                    block.getBytes(block.getStart(), buf);
                    int foundInBlock = 0;
                    for (int i = 0; i + 6 <= buf.length; i++) {
                        int op = buf[i] & 0xFF;
                        if (op != 0x8B) continue; // MOV r32, r/m32
                        int modrm = buf[i + 1] & 0xFF;
                        int mod = (modrm >> 6) & 0x3;
                        int rm = modrm & 0x7;
                        if (mod != 2) continue; // mod=10 (disp32 addressing)
                        if (rm == 4) continue; // SIB byte present, different encoding, skip
                        int d0 = buf[i + 2] & 0xFF, d1 = buf[i + 3] & 0xFF, d2 = buf[i + 4] & 0xFF, d3 = buf[i + 5] & 0xFF;
                        long disp = (d0 & 0xFF) | ((d1 & 0xFF) << 8) | ((d2 & 0xFF) << 16) | ((long) (d3 & 0xFF) << 24);
                        if ((disp & 0x80000000L) != 0) disp |= 0xFFFFFFFF00000000L;
                        if (disp != disp32) continue;
                        Address hitAddr = block.getStart().add(i);
                        Function f = fm.getFunctionContaining(hitAddr);
                        if (f == null) f = fm.getFunctionAt(hitAddr);
                        String fname = (f != null) ? f.getName() + " @ " + f.getEntryPoint() : "(no function boundary)";
                        w.println("  HIT @ " + hitAddr + "  in " + fname);
                        foundInBlock++;
                        totalMatches++;
                        if (totalMatches > 200) {
                            w.println("  ... (200+ matches, stopping this pattern early)");
                            break;
                        }
                    }
                    w.println("Block " + block.getName() + ": " + foundInBlock + " matches.");
                    if (totalMatches > 200) break;
                }
                w.println();
            }
        }
        println("Wrote report to " + outPath);
    }
}
