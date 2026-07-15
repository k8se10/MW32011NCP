// DecompileFuncs.java — Ghidra headless GhidraScript.
// Decompiles a list of functions (given as hex entry-point addresses in script args)
// and writes their pseudo-C to a report file, along with a raw disassembly listing
// for quick cross-reference of any addresses the decompiler couldn't resolve to
// a named cvar/global.
//
// Usage: -postScript DecompileFuncs.java <output_path> <addr1> <addr2> ...
//   e.g. -postScript DecompileFuncs.java out.txt 004292f0 004df870

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;

import java.io.FileWriter;
import java.io.PrintWriter;

public class DecompileFuncs extends GhidraScript {

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args == null || args.length < 2) {
            println("Usage: DecompileFuncs.java <output_path> <addr1> [addr2] ...");
            return;
        }
        String outPath = args[0];

        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);
        decomp.setSimplificationStyle("decompile");

        try (PrintWriter w = new PrintWriter(new FileWriter(outPath))) {
            for (int i = 1; i < args.length; i++) {
                String hex = args[i];
                Address addr = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(hex);
                Function func = currentProgram.getFunctionManager().getFunctionAt(addr);
                w.println("================================================================");
                if (func == null) {
                    // No function boundary defined here (e.g. a runtime callback pointer the
                    // auto-analyzer never reached) -- disassemble and create one on the fly.
                    if (getInstructionAt(addr) == null) {
                        disassemble(addr);
                    }
                    func = createFunction(addr, null);
                    if (func == null) {
                        w.println("No function found at " + hex + " (and could not create one)");
                        continue;
                    }
                }
                w.println("Function: " + func.getName() + " @ " + func.getEntryPoint());
                w.println("Signature: " + func.getSignature());
                w.println("----------------------------------------------------------------");

                DecompileResults results = decomp.decompileFunction(func, 60, monitor);
                if (results != null && results.decompileCompleted()) {
                    w.println(results.getDecompiledFunction().getC());
                } else {
                    w.println("DECOMPILE FAILED: " + (results != null ? results.getErrorMessage() : "null result"));
                }
                w.println();
            }
        }
        decomp.dispose();
        println("Wrote decompile report to " + outPath);
    }
}
