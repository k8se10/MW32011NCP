// FindCallersAt.java -- lists every caller (xref) of the function containing the given address,
// and for each caller, the enclosing function name + whether that caller itself has multiple callers
// (a cheap one-shot-vs-hot-path signal).
// Usage: -postScript FindCallersAt.java <output_path> <addr>
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.symbol.ReferenceManager;

import java.io.FileWriter;
import java.io.PrintWriter;

public class FindCallersAt extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args == null || args.length < 2) {
            println("Usage: FindCallersAt.java <output_path> <addr>");
            return;
        }
        String outPath = args[0];
        Address target = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(args[1]);
        FunctionManager fm = currentProgram.getFunctionManager();
        Function targetFn = fm.getFunctionContaining(target);
        ReferenceManager refMgr = currentProgram.getReferenceManager();

        try (PrintWriter w = new PrintWriter(new FileWriter(outPath))) {
            if (targetFn == null) {
                w.println("No function contains " + target);
                return;
            }
            w.println("Target function: " + targetFn.getName() + " @ " + targetFn.getEntryPoint());
            Address entry = targetFn.getEntryPoint();
            ReferenceIterator refs = refMgr.getReferencesTo(entry);
            int count = 0;
            while (refs.hasNext()) {
                Reference r = refs.next();
                Address fromAddr = r.getFromAddress();
                Function callerFn = fm.getFunctionContaining(fromAddr);
                count++;
                w.println("CALLER #" + count + ": from " + fromAddr +
                    " in function " + (callerFn == null ? "<none>" : callerFn.getName() + " @ " + callerFn.getEntryPoint()) +
                    " refType=" + r.getReferenceType());
                if (callerFn != null) {
                    // How many callers does THIS caller function itself have (cheap hot-path signal)
                    ReferenceIterator callerRefs = refMgr.getReferencesTo(callerFn.getEntryPoint());
                    int callerCallerCount = 0;
                    while (callerRefs.hasNext()) { callerRefs.next(); callerCallerCount++; }
                    w.println("    (caller function itself has " + callerCallerCount + " callers)");
                }
            }
            w.println("Total callers of " + targetFn.getName() + ": " + count);
        }
    }
}
