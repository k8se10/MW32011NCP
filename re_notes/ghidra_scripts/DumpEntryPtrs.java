// DumpEntryPtrs.java — reads the second pointer field of given {name*, kb*} table entry
// addresses and reports it, plus every reference TO that runtime pointer (both in
// functions and in data), to locate the per-frame kbutton-state consumer.
//
// Usage: -postScript DumpEntryPtrs.java <output_path> <entryAddr1> [<entryAddr2> ...]

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.symbol.ReferenceManager;

import java.io.FileWriter;
import java.io.PrintWriter;

public class DumpEntryPtrs extends GhidraScript {

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args == null || args.length < 2) {
            println("Usage: DumpEntryPtrs.java <output_path> <entryAddr1> ...");
            return;
        }
        String outPath = args[0];
        Memory mem = currentProgram.getMemory();
        FunctionManager fm = currentProgram.getFunctionManager();
        ReferenceManager refMgr = currentProgram.getReferenceManager();

        try (PrintWriter w = new PrintWriter(new FileWriter(outPath))) {
            for (int i = 1; i < args.length; i++) {
                Address entryAddr = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(args[i]);
                int namePtr = mem.getInt(entryAddr);
                int kbPtr = mem.getInt(entryAddr.add(4));
                Address kbAddr = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(kbPtr & 0xFFFFFFFFL);
                w.println("=== entry @ " + entryAddr + " kbutton_t* = " + kbAddr + " ===");

                ReferenceIterator refs = refMgr.getReferencesTo(kbAddr);
                int count = 0;
                while (refs.hasNext()) {
                    Reference ref = refs.next();
                    count++;
                    Address from = ref.getFromAddress();
                    Function func = fm.getFunctionContaining(from);
                    w.println("  from " + from + (func != null ? " IN FUNCTION " + func.getName() + "@" + func.getEntryPoint() : " IN DATA"));
                }
                w.println("  (" + count + " references)");
                w.println();
            }
        }
        println("Wrote report to " + outPath);
    }
}
