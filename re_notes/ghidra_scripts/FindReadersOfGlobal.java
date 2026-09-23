// FindReadersOfGlobal.java -- lists every function that references a given global address,
// and for each, whether it looks like a per-frame candidate (called from many places / large fn).
// Usage: -postScript FindReadersOfGlobal.java <output_path> <addr1> [addr2 ...]
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.symbol.ReferenceManager;

import java.io.FileWriter;
import java.io.PrintWriter;

public class FindReadersOfGlobal extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args == null || args.length < 2) {
            println("Usage: FindReadersOfGlobal.java <output_path> <addr1> [addr2 ...]");
            return;
        }
        String outPath = args[0];
        FunctionManager fm = currentProgram.getFunctionManager();
        ReferenceManager refMgr = currentProgram.getReferenceManager();

        try (PrintWriter w = new PrintWriter(new FileWriter(outPath))) {
            for (int i = 1; i < args.length; i++) {
                Address gAddr = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(args[i]);
                w.println("================================================================");
                w.println("Global: " + gAddr);
                ReferenceIterator refs = refMgr.getReferencesTo(gAddr);
                int count = 0;
                while (refs.hasNext()) {
                    Reference r = refs.next();
                    Address fromAddr = r.getFromAddress();
                    Function fn = fm.getFunctionContaining(fromAddr);
                    count++;
                    if (fn != null) {
                        w.println("  REF #" + count + ": from " + fromAddr + " in " + fn.getName() +
                            " @ " + fn.getEntryPoint() + " (fnSize=" + fn.getBody().getNumAddresses() + ")");
                    } else {
                        w.println("  REF #" + count + ": from " + fromAddr + " in <none/data>");
                    }
                }
                w.println("Total refs: " + count);
                w.println();
            }
        }
    }
}
