// DecompileContaining.java -- like DecompileFuncs.java, but for RETURN addresses off a
// real stack walk (CaptureStackBackTrace), which land mid-function, not at a function's
// entry point. DecompileFuncs.java's own getFunctionAt() returns null for these and
// falls back to forcibly CREATING a bogus function starting exactly at the return
// address -- garbage output, not the real containing function. This resolves via
// getFunctionContaining() instead, decompiling the REAL function and noting the real
// entry point address alongside the queried return address for cross-reference.
//
// Usage: -postScript DecompileContaining.java <output_path> <addr1> [addr2] ...

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;

import java.io.FileWriter;
import java.io.PrintWriter;

public class DecompileContaining extends GhidraScript {

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args == null || args.length < 2) {
            println("Usage: DecompileContaining.java <output_path> <addr1> [addr2] ...");
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
                Function func = currentProgram.getFunctionManager().getFunctionContaining(addr);
                w.println("================================================================");
                w.println("Queried return address: " + hex);
                if (func == null) {
                    w.println("No function CONTAINS this address (not yet disassembled/analyzed under -noanalysis).");
                    continue;
                }
                w.println("Containing function: " + func.getName() + " @ " + func.getEntryPoint());
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
        println("Wrote report to " + outPath);
    }
}
