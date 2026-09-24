// ScanVtableOffsetCalls.java -- raw-byte-scans every executable block for
// "CALL qword ptr [reg+disp32]" (FF /2, ModRM mod=10 reg=010 rm=0-7 except 4/RSP
// which needs a SIB byte, handled separately) where disp32 equals a given vtable
// slot byte offset -- i.e. finds every call-through-a-COM-vtable-slot site for a
// given slot, regardless of which register holds the vtable/interface pointer.
// Complements FindLeaRefsTo.java (data refs) for the "find every real caller of
// D3D9 method slot N" question when the interface pointer isn't a fixed global.
// Usage: -postScript ScanVtableOffsetCalls.java <output_path> <disp32_hex> [disp32_hex...]
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;

import java.io.FileWriter;
import java.io.PrintWriter;

public class ScanVtableOffsetCalls extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args == null || args.length < 2) {
            println("Usage: ScanVtableOffsetCalls.java <output_path> <disp32_hex> [disp32_hex...]");
            return;
        }
        String outPath = args[0];
        Memory mem = currentProgram.getMemory();
        FunctionManager fm = currentProgram.getFunctionManager();

        try (PrintWriter w = new PrintWriter(new FileWriter(outPath))) {
            for (int a = 1; a < args.length; a++) {
                long disp32 = Long.decode(args[a]);
                w.println("Searching for CALL [reg+0x" + Long.toHexString(disp32) + "] (FF /2 disp32 form)");
                int totalMatches = 0;
                for (MemoryBlock block : mem.getBlocks()) {
                    if (!block.isExecute()) continue;
                    long size = block.getSize();
                    byte[] buf = new byte[(int) Math.min(size, Integer.MAX_VALUE - 16)];
                    block.getBytes(block.getStart(), buf);
                    int foundInBlock = 0;
                    for (int i = 0; i + 6 <= buf.length; i++) {
                        int op = buf[i] & 0xFF;
                        if (op != 0xFF) continue;
                        int modrm = buf[i + 1] & 0xFF;
                        int mod = (modrm >> 6) & 0x3;
                        int reg = (modrm >> 3) & 0x7;
                        int rm = modrm & 0x7;
                        if (mod != 2 || reg != 2) continue; // mod=10 (disp32), reg=010 (CALL /2)
                        if (rm == 4) continue; // SIB byte present, different encoding, skip (rare for this pattern)
                        int d0 = buf[i + 2] & 0xFF, d1 = buf[i + 3] & 0xFF, d2 = buf[i + 4] & 0xFF, d3 = buf[i + 5] & 0xFF;
                        long disp = (d0 & 0xFF) | ((d1 & 0xFF) << 8) | ((d2 & 0xFF) << 16) | ((long) (d3 & 0xFF) << 24);
                        // sign-extend 32-bit
                        if ((disp & 0x80000000L) != 0) disp |= 0xFFFFFFFF00000000L;
                        if (disp != disp32) continue;
                        Address hitAddr = block.getStart().add(i);
                        Function f = fm.getFunctionContaining(hitAddr);
                        if (f == null) {
                            // try creating a function here for reporting purposes only (best-effort)
                            f = fm.getFunctionAt(hitAddr);
                        }
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
