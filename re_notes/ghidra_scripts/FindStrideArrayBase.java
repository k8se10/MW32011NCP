// FindStrideArrayBase.java — scans every instruction for "IMUL reg,reg,<stride>"
// immediately followed by "ADD (same reg), <constant>" -- the classic
// "arrayBase + index*stride" pattern -- and reports the resulting candidate array
// base address for each match. Used to find array bases that FindConstantRefs alone
// can't reveal (the base constant only appears combined with the stride via register
// arithmetic, not as one literal).
//
// Usage: -postScript FindStrideArrayBase.java <output_path> <strideDec>

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

public class FindStrideArrayBase extends GhidraScript {

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args == null || args.length < 2) {
            println("Usage: FindStrideArrayBase.java <output_path> <strideDec>");
            return;
        }
        String outPath = args[0];
        long stride = Long.parseLong(args[1]);

        Listing listing = currentProgram.getListing();
        FunctionManager fm = currentProgram.getFunctionManager();
        InstructionIterator it = listing.getInstructions(true);

        try (PrintWriter w = new PrintWriter(new FileWriter(outPath))) {
            int found = 0;
            while (it.hasNext()) {
                Instruction insn = it.next();
                if (!insn.getMnemonicString().equalsIgnoreCase("IMUL")) continue;
                if (insn.getNumOperands() < 3) continue;
                Object[] destObjs = insn.getOpObjects(0);
                Object[] srcObjs = insn.getOpObjects(2);
                if (srcObjs.length != 1 || !(srcObjs[0] instanceof Scalar)) continue;
                long val = ((Scalar) srcObjs[0]).getValue();
                if (val != stride) continue;

                Instruction next = insn.getNext();
                if (next == null) continue;
                if (!next.getMnemonicString().equalsIgnoreCase("ADD")) continue;
                if (next.getNumOperands() < 2) continue;
                Object[] nextDest = next.getOpObjects(0);
                Object[] nextSrc = next.getOpObjects(1);
                if (nextSrc.length != 1 || !(nextSrc[0] instanceof Scalar)) continue;
                // require same register between IMUL dest and ADD dest
                if (destObjs.length != 1 || nextDest.length != 1
                    || !destObjs[0].toString().equals(nextDest[0].toString())) continue;

                long base = ((Scalar) nextSrc[0]).getValue();
                Address at = insn.getAddress();
                Function func = fm.getFunctionContaining(at);
                w.println(String.format("STRIDE MATCH @ %s base=0x%x (in %s)", at,
                    base, func != null ? func.getName() + "@" + func.getEntryPoint() : "UNKNOWN"));
                found++;
            }
            w.println("Total matches: " + found);
        }
        println("Wrote report to " + outPath);
    }
}
