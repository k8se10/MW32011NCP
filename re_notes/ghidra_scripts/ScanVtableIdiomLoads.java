// ScanVtableIdiomLoads.java -- like ScanVtableOffsetLoads.java but requires the
// exact real idiom confirmed 2026-09-24 against x86's own known BeginScene call
// site: a plain "MOV reg,[reg2]" (0x8B /r, mod=00, no displacement -- a COM
// object's vtable-pointer dereference) landing IMMEDIATELY before a "MOV
// reg3,[reg+disp32]" (the vtable slot load) with the given disp32. This filters
// out coincidental unrelated struct-field accesses at the same small offset,
// which the plain offset-only scan (ScanVtableOffsetLoads.java) can't distinguish.
//
// Usage: -postScript ScanVtableIdiomLoads.java <output_path> <disp32_hex> [disp32_hex...]
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;

import java.io.FileWriter;
import java.io.PrintWriter;

public class ScanVtableIdiomLoads extends GhidraScript {
    // Returns length of a "MOV reg,[reg2]" (8B /r, mod=00, rm != 4/5) instruction
    // ending exactly at position `end` in buf, or -1 if none matches there.
    private int matchPrecedingPlainDeref(byte[] buf, int end) {
        // Instruction is 2 bytes: 8B <modrm>. Check buf[end-2], buf[end-1].
        if (end - 2 < 0) return -1;
        int op = buf[end - 2] & 0xFF;
        if (op != 0x8B) return -1;
        int modrm = buf[end - 1] & 0xFF;
        int mod = (modrm >> 6) & 0x3;
        int rm = modrm & 0x7;
        if (mod != 0) return -1;
        if (rm == 4 || rm == 5) return -1; // SIB or disp32-only special case
        return 2;
    }

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args == null || args.length < 2) {
            println("Usage: ScanVtableIdiomLoads.java <output_path> <disp32_hex> [disp32_hex...]");
            return;
        }
        String outPath = args[0];
        Memory mem = currentProgram.getMemory();
        FunctionManager fm = currentProgram.getFunctionManager();

        try (PrintWriter w = new PrintWriter(new FileWriter(outPath))) {
            for (int a = 1; a < args.length; a++) {
                long disp32 = Long.decode(args[a]);
                w.println("Searching for MOV reg,[reg2] ; MOV reg3,[reg2b+0x" + Long.toHexString(disp32) + "] idiom");
                int totalMatches = 0;
                for (MemoryBlock block : mem.getBlocks()) {
                    if (!block.isExecute()) continue;
                    long size = block.getSize();
                    byte[] buf = new byte[(int) Math.min(size, Integer.MAX_VALUE - 16)];
                    block.getBytes(block.getStart(), buf);
                    int foundInBlock = 0;
                    for (int i = 0; i + 6 <= buf.length; i++) {
                        int op = buf[i] & 0xFF;
                        if (op != 0x8B) continue;
                        int modrm = buf[i + 1] & 0xFF;
                        int mod = (modrm >> 6) & 0x3;
                        int rm = modrm & 0x7;
                        if (mod != 2) continue;
                        if (rm == 4) continue;
                        int d0 = buf[i + 2] & 0xFF, d1 = buf[i + 3] & 0xFF, d2 = buf[i + 4] & 0xFF, d3 = buf[i + 5] & 0xFF;
                        long disp = (d0 & 0xFF) | ((d1 & 0xFF) << 8) | ((d2 & 0xFF) << 16) | ((long) (d3 & 0xFF) << 24);
                        if ((disp & 0x80000000L) != 0) disp |= 0xFFFFFFFF00000000L;
                        if (disp != disp32) continue;
                        // Require the idiom: a plain "MOV reg,[reg2]" immediately before this instruction.
                        if (matchPrecedingPlainDeref(buf, i) < 0) continue;
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
