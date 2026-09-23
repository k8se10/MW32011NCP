// DecompileAndCallersAt.java -- decompiles the function containing each address, AND lists its own callers.
// Usage: -postScript DecompileAndCallersAt.java <output_path> <addr1> [addr2 ...]
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.symbol.ReferenceManager;

import java.io.FileWriter;
import java.io.PrintWriter;

public class DecompileAndCallersAt extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args == null || args.length < 2) {
            println("Usage: DecompileAndCallersAt.java <output_path> <addr1> [addr2 ...]");
            return;
        }
        String outPath = args[0];
        FunctionManager fm = currentProgram.getFunctionManager();
        ReferenceManager refMgr = currentProgram.getReferenceManager();

        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);
        decomp.setSimplificationStyle("decompile");

        try (PrintWriter w = new PrintWriter(new FileWriter(outPath))) {
            for (int i = 1; i < args.length; i++) {
                Address addr = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(args[i]);
                Function f = fm.getFunctionContaining(addr);
                w.println("================================================================");
                if (f == null) {
                    w.println("No function contains address " + addr);
                    w.println();
                    continue;
                }
                w.println("Function: " + f.getName() + " @ " + f.getEntryPoint());
                w.println("----------------------------------------------------------------");
                DecompileResults results = decomp.decompileFunction(f, 60, monitor);
                if (results != null && results.decompileCompleted()) {
                    w.println(results.getDecompiledFunction().getC());
                } else {
                    w.println("DECOMPILE FAILED: " + (results != null ? results.getErrorMessage() : "null result"));
                }
                w.println("---- CALLERS of " + f.getName() + " ----");
                ReferenceIterator refs = refMgr.getReferencesTo(f.getEntryPoint());
                int count = 0;
                while (refs.hasNext()) {
                    Reference r = refs.next();
                    Address fromAddr = r.getFromAddress();
                    Function callerFn = fm.getFunctionContaining(fromAddr);
                    count++;
                    w.println("  CALLER #" + count + ": from " + fromAddr + " in " +
                        (callerFn == null ? "<none/data>" : callerFn.getName() + " @ " + callerFn.getEntryPoint()) +
                        " refType=" + r.getReferenceType());
                }
                w.println("Total callers: " + count);
                w.println();
            }
        }
    }
}
