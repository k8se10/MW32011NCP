// CreateAndDecompileAt.java -- disassembles + creates a function at each given
// address if none exists yet (this project runs Ghidra with -noanalysis for
// speed, so no Function object exists at most addresses), then decompiles it.
// Usage: -postScript CreateAndDecompileAt.java <output_path> <addr1> [addr2 ...]
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.cmd.function.CreateFunctionCmd;

import java.io.FileWriter;
import java.io.PrintWriter;

public class CreateAndDecompileAt extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args == null || args.length < 2) {
            println("Usage: CreateAndDecompileAt.java <output_path> <addr1> [addr2 ...]");
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
                if (f == null) {
                    DisassembleCommand disCmd = new DisassembleCommand(addr, null, true);
                    disCmd.applyTo(currentProgram, monitor);
                    CreateFunctionCmd funcCmd = new CreateFunctionCmd(addr);
                    funcCmd.applyTo(currentProgram, monitor);
                    f = fm.getFunctionAt(addr);
                }
                w.println("================================================================");
                if (f == null) {
                    w.println("Could not create/find function at " + addr);
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
