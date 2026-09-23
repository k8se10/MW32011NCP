// FindImportSlotAndCallers.java -- finds the real IAT memory address for a named import,
// then raw-byte-scans the whole main executable block for "FF 15 <rel32>" (CALL qword ptr
// [rip+disp32]) instructions whose computed target equals that IAT slot. Works without a
// full analysis pass since it doesn't rely on Ghidra's own disassembly/reference data --
// pure byte pattern + arithmetic, same class of technique as this project's own
// PatternScanMP.java / SigScan::ResolveRipRelative.
// Usage: -postScript FindImportSlotAndCallers.java <output_path> <importSymbolName>
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolIterator;
import ghidra.program.model.symbol.SymbolTable;

import java.io.FileWriter;
import java.io.PrintWriter;

public class FindImportSlotAndCallers extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args == null || args.length < 2) {
            println("Usage: FindImportSlotAndCallers.java <output_path> <importSymbolName>");
            return;
        }
        String outPath = args[0];
        String target = args[1];
        SymbolTable st = currentProgram.getSymbolTable();
        Memory mem = currentProgram.getMemory();

        try (PrintWriter w = new PrintWriter(new FileWriter(outPath))) {
            // Find every symbol with this name -- imports typically have both an
            // EXTERNAL pseudo-symbol AND a real memory-mapped one in .idata/IAT.
            SymbolIterator it = st.getSymbols(target);
            java.util.List<Address> realAddrs = new java.util.ArrayList<>();
            while (it.hasNext()) {
                Symbol s = it.next();
                Address a = s.getAddress();
                w.println("Symbol: " + s.getName() + " @ " + a + " isExternal=" + a.isExternalAddress()
                        + " space=" + a.getAddressSpace().getName());
                if (!a.isExternalAddress()) {
                    realAddrs.add(a);
                }
            }
            if (realAddrs.isEmpty()) {
                w.println("No non-external (real memory) address found for " + target
                        + " -- cannot scan for indirect callers this way.");
                return;
            }

            // Scan every executable memory block for FF 15 xx xx xx xx, compute the
            // real target, and report matches against any of realAddrs.
            FunctionManager fm = currentProgram.getFunctionManager();
            for (MemoryBlock block : mem.getBlocks()) {
                if (!block.isExecute()) continue;
                w.println("Scanning block " + block.getName() + " [" + block.getStart() + " - " + block.getEnd() + "]");
                long size = block.getSize();
                byte[] buf = new byte[(int) Math.min(size, Integer.MAX_VALUE - 16)];
                block.getBytes(block.getStart(), buf);
                int foundCount = 0;
                for (int i = 0; i + 6 <= buf.length; i++) {
                    if ((buf[i] & 0xFF) == 0xFF && (buf[i + 1] & 0xFF) == 0x15) {
                        int disp = (buf[i + 2] & 0xFF) | ((buf[i + 3] & 0xFF) << 8)
                                | ((buf[i + 4] & 0xFF) << 16) | ((buf[i + 5] & 0xFF) << 24);
                        Address insnAddr = block.getStart().add(i);
                        Address nextInsn = insnAddr.add(6);
                        Address computedTarget;
                        try {
                            computedTarget = nextInsn.add(disp);
                        } catch (Exception e) {
                            continue;
                        }
                        for (Address realAddr : realAddrs) {
                            if (computedTarget.equals(realAddr)) {
                                Function f = fm.getFunctionContaining(insnAddr);
                                w.println("  MATCH: CALL [rip+0x" + Integer.toHexString(disp) + "] at "
                                        + insnAddr + " -> " + computedTarget
                                        + "  (containing function: " + (f != null ? f.getName() + "@" + f.getEntryPoint() : "NONE/unresolved") + ")");
                                foundCount++;
                            }
                        }
                    }
                }
                w.println("  " + foundCount + " matches in this block.");
            }
        }
        println("Wrote report to " + outPath);
    }
}
