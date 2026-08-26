import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;

public class DumpCallOperandSample extends GhidraScript {
    @Override
    public void run() throws Exception {
        InstructionIterator it = currentProgram.getListing().getInstructions(true);
        int shown = 0;
        int total = 0;
        while (it.hasNext() && !monitor.isCancelled() && shown < 40) {
            Instruction insn = it.next();
            if (!insn.getMnemonicString().equalsIgnoreCase("CALL")) continue;
            total++;
            String rep = insn.toString();
            if (rep.toUpperCase().contains("RAX") || rep.contains("[")) {
                println(insn.getAddress() + "  " + rep + "   nOps=" + insn.getNumOperands());
                shown++;
            }
        }
        println("total CALL instructions seen (sample cap 40 shown): scanned enough to find 40 bracket ones, shown=" + shown);
    }
}
