// FindLockPrefixHotFuncs.java -- scans .text for LOCK-prefixed INC/DEC/XADD/CMPXCHG
// instructions (0xF0 0xFF /0 or /1, 0xF0 0x0F 0xC1, 0xF0 0x0F 0xB1/0xB0), finds the
// containing function for each hit (requires a full-analysis project opened with
// -process, not a fresh -noanalysis import, so function boundaries already exist),
// then reports each candidate function's caller count via the reference manager --
// a real, generic, widely-shared lock (like x86 FindOrLoadAsset's, 59 callers) should
// stand out with a high caller count among an otherwise-small set of candidates.
// Usage: -postScript FindLockPrefixHotFuncs.java <output_path> [minCallers]
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.symbol.ReferenceManager;

import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.LinkedHashSet;
import java.util.Set;

public class FindLockPrefixHotFuncs extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args == null || args.length < 1) {
            println("Usage: FindLockPrefixHotFuncs.java <output_path> [minCallers]");
            return;
        }
        String outPath = args[0];
        int minCallers = args.length > 1 ? Integer.parseInt(args[1]) : 0;

        Memory mem = currentProgram.getMemory();
        FunctionManager fm = currentProgram.getFunctionManager();
        ReferenceManager rm = currentProgram.getReferenceManager();

        Set<Function> candidates = new LinkedHashSet<>();

        try (PrintWriter w = new PrintWriter(new FileWriter(outPath))) {
            for (MemoryBlock block : mem.getBlocks()) {
                if (!block.isExecute()) continue;
                long size = block.getSize();
                byte[] buf = new byte[(int) Math.min(size, Integer.MAX_VALUE - 16)];
                long blockStart = block.getStart().getOffset();
                block.getBytes(block.getStart(), buf);
                for (int i = 0; i + 4 <= buf.length; i++) {
                    int b0 = buf[i] & 0xFF;
                    if (b0 != 0xF0) continue; // LOCK prefix
                    int b1 = buf[i + 1] & 0xFF;
                    int b2 = buf[i + 2] & 0xFF;
                    boolean isLockIncDec = (b1 == 0xFF) && (((b2 >> 3) & 7) <= 1); // /0=INC /1=DEC
                    boolean isLockXadd = (b1 == 0x0F) && (b2 == 0xC1);
                    boolean isLockCmpxchg = (b1 == 0x0F) && (b2 == 0xB1 || b2 == 0xB0);
                    if (!isLockIncDec && !isLockXadd && !isLockCmpxchg) continue;

                    Address hitAddr = block.getStart().add(i);
                    Function f = fm.getFunctionContaining(hitAddr);
                    if (f == null) continue;
                    candidates.add(f);
                }
            }

            w.println("Found " + candidates.size() + " distinct functions containing a LOCK-prefixed inc/dec/xadd/cmpxchg.");
            w.println("Reporting those with >= " + minCallers + " callers, sorted by caller count desc:");
            w.println();

            java.util.List<Function> sorted = new java.util.ArrayList<>(candidates);
            java.util.Map<Function, Integer> callerCounts = new java.util.HashMap<>();
            for (Function f : sorted) {
                int count = 0;
                ReferenceIterator refs = rm.getReferencesTo(f.getEntryPoint());
                while (refs.hasNext()) {
                    Reference r = refs.next();
                    if (r.getReferenceType().isCall()) count++;
                }
                callerCounts.put(f, count);
            }
            sorted.sort((a, b) -> callerCounts.get(b) - callerCounts.get(a));

            for (Function f : sorted) {
                int count = callerCounts.get(f);
                if (count < minCallers) continue;
                w.println("  " + f.getName() + " @ " + f.getEntryPoint()
                    + "  callers=" + count
                    + "  paramCount=" + f.getParameterCount()
                    + "  bodySize=" + f.getBody().getNumAddresses());
            }
        }
        println("Wrote report to " + outPath);
    }
}
