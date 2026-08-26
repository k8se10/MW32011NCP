// DecompileAt.java -- decompiles the function containing each given address directly.
// Usage: -postScript DecompileAt.java <output_path> <addr1> [addr2 ...]
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;

import java.io.FileWriter;
import java.io.PrintWriter;

public class DecompileAt extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args == null || args.length < 2) {
            println("Usage: DecompileAt.java <output_path> <addr1> [addr2 ...]");
            return;
        }
        String outPath = args[0];
        FunctionManager fm = currentProgram.getFunctionManager();

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
                    w.println("DECOMPILE FAILED");
                }
                w.println();
            }
        }
        decomp.dispose();
        println("Wrote report to " + outPath);
    }
}
