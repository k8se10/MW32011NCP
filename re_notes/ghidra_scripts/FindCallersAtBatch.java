// FindCallersAtBatch.java -- lists callers for multiple addresses in one run (cheaper than
// separate headless invocations per address).
// Usage: -postScript FindCallersAtBatch.java <output_path> <addr1> [addr2 ...]
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.symbol.ReferenceManager;

import java.io.FileWriter;
import java.io.PrintWriter;

public class FindCallersAtBatch extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args == null || args.length < 2) {
            println("Usage: FindCallersAtBatch.java <output_path> <addr1> [addr2 ...]");
            return;
        }
        String outPath = args[0];
        FunctionManager fm = currentProgram.getFunctionManager();
        ReferenceManager refMgr = currentProgram.getReferenceManager();

        try (PrintWriter w = new PrintWriter(new FileWriter(outPath))) {
            for (int ai = 1; ai < args.length; ai++) {
                Address target = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(args[ai]);
                Function targetFn = fm.getFunctionContaining(target);
                w.println("================================================================");
                if (targetFn == null) {
                    w.println("No function contains " + target);
                    continue;
                }
                w.println("Target: " + targetFn.getName() + " @ " + targetFn.getEntryPoint());
                Address entry = targetFn.getEntryPoint();
                ReferenceIterator refs = refMgr.getReferencesTo(entry);
                int count = 0;
                while (refs.hasNext()) {
                    Reference r = refs.next();
                    Address fromAddr = r.getFromAddress();
                    Function callerFn = fm.getFunctionContaining(fromAddr);
                    count++;
                    w.println("  CALLER #" + count + ": from " + fromAddr + " in " +
                        (callerFn == null ? "<none/data>" : callerFn.getName() + " @ " + callerFn.getEntryPoint()));
                }
                w.println("Total: " + count);
            }
        }
    }
}
