// FindDirectCallers.java -- raw-byte-scans .text for direct "CALL rel32" (0xE8)
// instructions targeting a given absolute address, computing the real target from
// each E8's own relative displacement. Works under -noanalysis where the
// reference manager hasn't seen the call site yet. Complements
// DecompileAndCallersAt.java (reference-manager-based, needs prior disassembly).
// Usage: -postScript FindDirectCallers.java <output_path> <targetAddr> [targetAddr...]
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;

import java.io.FileWriter;
import java.io.PrintWriter;

public class FindDirectCallers extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args == null || args.length < 2) {
            println("Usage: FindDirectCallers.java <output_path> <targetAddr> [targetAddr...]");
            return;
        }
        String outPath = args[0];
        Memory mem = currentProgram.getMemory();
        FunctionManager fm = currentProgram.getFunctionManager();
        long imageBase = currentProgram.getImageBase().getOffset();

        try (PrintWriter w = new PrintWriter(new FileWriter(outPath))) {
            for (int a = 1; a < args.length; a++) {
                long target = Long.decode(args[a]);
                w.println("Searching for CALL rel32 targeting 0x" + Long.toHexString(target));
                int totalMatches = 0;
                for (MemoryBlock block : mem.getBlocks()) {
                    if (!block.isExecute()) continue;
                    long size = block.getSize();
                    byte[] buf = new byte[(int) Math.min(size, Integer.MAX_VALUE - 16)];
                    long blockStart = block.getStart().getOffset();
                    block.getBytes(block.getStart(), buf);
                    for (int i = 0; i + 5 <= buf.length; i++) {
                        if ((buf[i] & 0xFF) != 0xE8) continue;
                        int d0 = buf[i + 1] & 0xFF, d1 = buf[i + 2] & 0xFF, d2 = buf[i + 3] & 0xFF, d3 = buf[i + 4] & 0xFF;
                        long rel = (d0 & 0xFF) | ((d1 & 0xFF) << 8) | ((d2 & 0xFF) << 16) | ((long) (d3 & 0xFF) << 24);
                        if ((rel & 0x80000000L) != 0) rel |= 0xFFFFFFFF00000000L;
                        long insnAddr = blockStart + i;
                        long nextInsnAddr = insnAddr + 5;
                        long callTarget = (nextInsnAddr + rel) & 0xFFFFFFFFL;
                        if (callTarget != target) continue;
                        Address hitAddr = block.getStart().add(i);
                        Function f = fm.getFunctionContaining(hitAddr);
                        String fname = (f != null) ? f.getName() + " @ " + f.getEntryPoint() : "(no function boundary)";
                        w.println("  CALLER @ " + hitAddr + "  in " + fname);
                        totalMatches++;
                        if (totalMatches > 200) {
                            w.println("  ... (200+ matches, stopping)");
                            break;
                        }
                    }
                    if (totalMatches > 200) break;
                }
                w.println("Total: " + totalMatches + " matches.");
                w.println();
            }
        }
        println("Wrote report to " + outPath);
    }
}
