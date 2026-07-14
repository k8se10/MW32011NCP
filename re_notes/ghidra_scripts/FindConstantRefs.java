// FindConstantRefs.java — scans EVERY instruction in the whole program for a scalar
// operand matching any of the given hex constants (decimal-free, e.g. fused
// "base+offset" LEA/CMP immediates that never show up as a formal Reference because
// they're runtime-computed from a register, not a static data reference). Used to find
// which function(s) touch a specific struct offset when DescribeRefs/xref search comes
// up empty (common for register-relative accesses in this binary).
//
// Usage: -postScript FindConstantRefs.java <output_path> <hexConst1> [hexConst2 ...]

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.scalar.Scalar;

import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.HashSet;
import java.util.Set;

public class FindConstantRefs extends GhidraScript {

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args == null || args.length < 2) {
            println("Usage: FindConstantRefs.java <output_path> <hexConst1> [hexConst2 ...]");
            return;
        }
        String outPath = args[0];
        Set<Long> targets = new HashSet<>();
        for (int i = 1; i < args.length; i++) {
            targets.add(Long.parseLong(args[i], 16) & 0xFFFFFFFFL);
        }

        Listing listing = currentProgram.getListing();
        FunctionManager fm = currentProgram.getFunctionManager();
        InstructionIterator it = listing.getInstructions(true);

        try (PrintWriter w = new PrintWriter(new FileWriter(outPath))) {
            int scanned = 0;
            while (it.hasNext()) {
                Instruction insn = it.next();
                scanned++;
                int numOps = insn.getNumOperands();
                for (int op = 0; op < numOps; op++) {
                    Object[] objs = insn.getOpObjects(op);
                    for (Object o : objs) {
                        if (o instanceof Scalar) {
                            long val = ((Scalar) o).getUnsignedValue() & 0xFFFFFFFFL;
                            if (targets.contains(val)) {
                                Address at = insn.getAddress();
                                Function func = fm.getFunctionContaining(at);
                                w.println(String.format("0x%08x", val) + "  at " + at
                                    + (func != null ? " IN FUNCTION " + func.getName() + "@" + func.getEntryPoint() : " IN DATA")
                                    + "  :  " + insn.toString());
                            }
                        }
                    }
                }
                if (scanned % 500000 == 0) {
                    println("...scanned " + scanned + " instructions");
                }
            }
            w.println("Total instructions scanned: " + scanned);
        }
        println("Wrote report to " + outPath);
    }
}
