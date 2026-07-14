// FuncAtAddr.java — reports which function contains each given address, plus the
// nearest preceding instruction, for mapping a crash offset back to source.
//
// Usage: -postScript FuncAtAddr.java <output_path> <addr1> [addr2 ...]

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.Listing;

import java.io.FileWriter;
import java.io.PrintWriter;

public class FuncAtAddr extends GhidraScript {

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args == null || args.length < 2) {
            println("Usage: FuncAtAddr.java <output_path> <addr1> [addr2 ...]");
            return;
        }
        String outPath = args[0];
        Listing listing = currentProgram.getListing();

        try (PrintWriter w = new PrintWriter(new FileWriter(outPath))) {
            for (int i = 1; i < args.length; i++) {
                Address addr = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(args[i]);
                Function f = currentProgram.getFunctionManager().getFunctionContaining(addr);
                w.println("Address " + addr + ":");
                if (f != null) {
                    w.println("  IN FUNCTION " + f.getName() + " @ " + f.getEntryPoint()
                        + "  (offset +0x" + Long.toHexString(addr.subtract(f.getEntryPoint())) + ")");
                } else {
                    w.println("  NOT inside any known function");
                }
                Instruction insn = listing.getInstructionAt(addr);
                if (insn != null) {
                    w.println("  instruction at addr: " + insn);
                } else {
                    Instruction before = listing.getInstructionBefore(addr);
                    w.println("  no instruction exactly at addr; nearest before: " + before);
                }
                w.println();
            }
        }
        println("Wrote report to " + outPath);
    }
}
