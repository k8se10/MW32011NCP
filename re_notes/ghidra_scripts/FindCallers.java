// FindCallers.java — lists every function that CALLS a given function (not data refs to
// it), i.e. proper call-reference callers, plus a short decompile of each caller.
//
// Usage: -postScript FindCallers.java <output_path> <funcAddr>

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
import java.util.LinkedHashSet;
import java.util.Set;

public class FindCallers extends GhidraScript {

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args == null || args.length < 2) {
            println("Usage: FindCallers.java <output_path> <funcAddr>");
            return;
        }
        String outPath = args[0];
        Address target = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(args[1]);

        FunctionManager fm = currentProgram.getFunctionManager();
        ReferenceManager refMgr = currentProgram.getReferenceManager();

        Set<Function> callers = new LinkedHashSet<>();
        ReferenceIterator refs = refMgr.getReferencesTo(target);
        while (refs.hasNext()) {
            Reference ref = refs.next();
            if (!ref.getReferenceType().isCall()) continue;
            Function f = fm.getFunctionContaining(ref.getFromAddress());
            if (f != null) callers.add(f);
        }

        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);
        decomp.setSimplificationStyle("decompile");

        try (PrintWriter w = new PrintWriter(new FileWriter(outPath))) {
            w.println("Callers of " + target + " (" + callers.size() + "):");
            for (Function f : callers) {
                w.println("  " + f.getName() + " @ " + f.getEntryPoint());
            }
            w.println();
            for (Function f : callers) {
                w.println("================================================================");
                w.println("Function: " + f.getName() + " @ " + f.getEntryPoint());
                w.println("----------------------------------------------------------------");
                DecompileResults results = decomp.decompileFunction(f, 60, monitor);
                if (results != null && results.decompileCompleted()) {
                    w.println(results.getDecompiledFunction().getC());
                } else {
                    w.println("DECOMPILE FAILED");
                }
                w.println();
            }
        }
        decomp.dispose();
        println("Wrote report to " + outPath);
    }
}
