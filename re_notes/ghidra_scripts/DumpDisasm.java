// DumpDisasm.java — dumps raw disassembly (mnemonic + operands) for a function, so
// immediate constants the decompiler drops/obscures (e.g. call arguments loaded via
// mov/push before an unresolved-signature call) are visible.
//
// Usage: -postScript DumpDisasm.java <output_path> <funcAddr>

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.listing.Listing;

import java.io.FileWriter;
import java.io.PrintWriter;

public class DumpDisasm extends GhidraScript {

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args == null || args.length < 2) {
            println("Usage: DumpDisasm.java <output_path> <funcAddr>");
            return;
        }
        String outPath = args[0];
        Address funcAddr = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(args[1]);
        Function func = currentProgram.getFunctionManager().getFunctionAt(funcAddr);
        if (func == null) {
            println("No function at " + args[1]);
            return;
        }

        Listing listing = currentProgram.getListing();
        InstructionIterator it = listing.getInstructions(func.getBody(), true);

        try (PrintWriter w = new PrintWriter(new FileWriter(outPath))) {
            w.println("Disassembly of " + func.getName() + " @ " + func.getEntryPoint());
            while (it.hasNext()) {
                Instruction insn = it.next();
                w.println(insn.getAddress() + "  " + insn.toString());
            }
        }
        println("Wrote report to " + outPath);
    }
}
