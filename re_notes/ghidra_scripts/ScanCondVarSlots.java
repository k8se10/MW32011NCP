// ScanCondVarSlots.java -- raw byte-pattern scan for "FF 15 <rel32>" (CALL qword ptr
// [rip+disp32]) instructions targeting one of several given, manually-computed IAT slot
// addresses. Built because FindImportSlotAndCallers.java's SymbolTable.getSymbols() lookup
// found only EXTERNAL pseudo-symbols for these imports (no real memory-mapped IAT symbol
// exists under -noanalysis for them) -- this script takes the real addresses directly
// instead, computed by hand from dumpbin /imports' own printed IAT base + function's
// 0-indexed table position * 8 bytes (x64 pointer size).
// Usage: -postScript ScanCondVarSlots.java <output_path> <hexAddr1> [hexAddr2 ...]
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;

import java.io.FileWriter;
import java.io.PrintWriter;

public class ScanCondVarSlots extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args == null || args.length < 2) {
            println("Usage: ScanCondVarSlots.java <output_path> <hexAddr1> [hexAddr2 ...]");
            return;
        }
        String outPath = args[0];
        Memory mem = currentProgram.getMemory();
        FunctionManager fm = currentProgram.getFunctionManager();

        java.util.List<Address> targets = new java.util.ArrayList<>();
        for (int i = 1; i < args.length; i++) {
            targets.add(currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(args[i]));
        }

        try (PrintWriter w = new PrintWriter(new FileWriter(outPath))) {
            for (Address t : targets) w.println("Target: " + t);
            w.println();

            for (MemoryBlock block : mem.getBlocks()) {
                if (!block.isExecute()) continue;
                long size = block.getSize();
                byte[] buf = new byte[(int) size];
                try {
                    mem.getBytes(block.getStart(), buf);
                } catch (Exception e) {
                    continue;
                }
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
                        for (Address t : targets) {
                            if (computedTarget.equals(t)) {
                                Function fn = fm.getFunctionContaining(insnAddr);
                                w.println("MATCH target=" + t + " call@=" + insnAddr + " in="
                                        + (fn == null ? "<none>" : fn.getName() + " @ " + fn.getEntryPoint()));
                            }
                        }
                    }
                }
            }
            w.println("Scan complete.");
        }
    }
}
