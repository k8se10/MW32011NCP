// Broader than FindVtableCallSites: matches ANY instruction (not just CALL) whose
// operand text contains "+ 0xNN]" for a given displacement -- catches the common
// compiler pattern of loading a vtable function pointer into a register first
// (MOV reg, [vtableReg + 0xbc]) followed by a register-only CALL, not just a
// single fused indirect-memory CALL.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.listing.Function;
import ghidra.program.model.address.Address;

import java.util.HashSet;
import java.util.Set;

public class FindDisplacementRefs extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        Set<String> targets = new HashSet<>();
        if (args.length == 0) {
            targets.add("0xbc");
            targets.add("0xc0");
        } else {
            for (String a : args) targets.add(a.toLowerCase());
        }
        println("Scanning ALL instructions for operand displacement in: " + targets);

        InstructionIterator it = currentProgram.getListing().getInstructions(true);
        int hits = 0;
        while (it.hasNext() && !monitor.isCancelled()) {
            Instruction insn = it.next();
            String rep = insn.toString();
            String lower = rep.toLowerCase();
            if (!lower.contains("[") || !lower.contains("]")) continue;
            for (String disp : targets) {
                String needle1 = "+ " + disp + "]";
                String needle2 = "+" + disp + "]";
                if (lower.contains(needle1) || lower.contains(needle2)) {
                    Address addr = insn.getAddress();
                    Function f = getFunctionContaining(addr);
                    String fname = (f != null) ? f.getName() + "@" + f.getEntryPoint() : "???";
                    println("HIT disp=" + disp + "  " + addr + "  in " + fname + "   [" + rep + "]");
                    hits++;
                    break;
                }
            }
        }
        println("Total hits: " + hits);
    }
}
