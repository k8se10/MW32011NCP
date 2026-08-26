// Finds every instruction that WRITES to a given absolute data address (not just any
// reference) -- for confirming where a specific struct field (e.g. the renderer's
// cached render-target width/height) gets its value from.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.symbol.ReferenceManager;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;

import java.util.LinkedHashSet;
import java.util.Set;

public class FindDataWriters extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 1) {
            println("Usage: FindDataWriters.java <addr1> [addr2] ...");
            return;
        }
        ReferenceManager refMgr = currentProgram.getReferenceManager();
        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);
        decomp.setSimplificationStyle("decompile");

        for (String hex : args) {
            Address target = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(hex);
            println("=== References to " + target + " ===");
            ReferenceIterator refs = refMgr.getReferencesTo(target);
            Set<Function> funcsWithWrite = new LinkedHashSet<>();
            while (refs.hasNext()) {
                Reference ref = refs.next();
                Address from = ref.getFromAddress();
                Instruction insn = getInstructionAt(from);
                String rep = insn != null ? insn.toString() : "?";
                boolean isWrite = ref.getReferenceType().isWrite() ||
                        (insn != null && insn.getMnemonicString().equalsIgnoreCase("MOV") && rep.trim().startsWith("MOV dword ptr") );
                Function f = getFunctionContaining(from);
                String fname = (f != null) ? f.getName() + "@" + f.getEntryPoint() : "???";
                println((isWrite ? "WRITE " : "read? ") + from + " in " + fname + "  [" + rep + "]  refType=" + ref.getReferenceType());
                if (isWrite && f != null) funcsWithWrite.add(f);
            }
            for (Function f : funcsWithWrite) {
                println("--- decompile of writer " + f.getName() + "@" + f.getEntryPoint() + " ---");
                DecompileResults results = decomp.decompileFunction(f, 60, monitor);
                if (results != null && results.decompileCompleted()) {
                    println(results.getDecompiledFunction().getC());
                } else {
                    println("DECOMPILE FAILED");
                }
            }
            println("");
        }
        decomp.dispose();
    }
}
