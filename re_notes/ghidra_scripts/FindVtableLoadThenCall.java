// Finds the real compiler pattern for a COM vtable dispatch split across two
// instructions: "MOV <reg>, dword ptr [<base> + 0xNN]" (load the method pointer
// out of the vtable) followed within a few instructions by "CALL <reg>" using
// THE SAME destination register -- much more specific than a raw displacement
// text search (which is swamped by ordinary stack-frame [ESP+0xNN] noise).
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.listing.Function;
import ghidra.program.model.address.Address;

import java.util.HashSet;
import java.util.Set;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

public class FindVtableLoadThenCall extends GhidraScript {
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
        println("Scanning for MOV reg,[base+disp] -> CALL reg pattern, disp in: " + targets);

        // e.g. "MOV EAX,dword ptr [ECX + 0xbc]"
        Pattern movPat = Pattern.compile("^MOV\\s+(E[A-Z]{2}),dword ptr \\[[A-Z]{2,3}(?:\\s*\\+\\s*(0x[0-9a-f]+))?\\]$", Pattern.CASE_INSENSITIVE);

        InstructionIterator it = currentProgram.getListing().getInstructions(true);
        int hits = 0;
        while (it.hasNext() && !monitor.isCancelled()) {
            Instruction insn = it.next();
            String rep = insn.toString().replaceAll("\\s+", " ").trim();
            Matcher m = movPat.matcher(rep);
            if (!m.matches()) continue;
            String reg = m.group(1);
            String disp = m.group(2);
            if (disp == null) continue;
            disp = disp.toLowerCase();
            if (!targets.contains(disp)) continue;

            // look ahead up to 4 instructions for CALL <reg>
            Instruction cur = insn;
            for (int i = 0; i < 4; i++) {
                cur = cur.getNext();
                if (cur == null) break;
                String crep = cur.toString().trim();
                if (crep.equalsIgnoreCase("CALL " + reg)) {
                    Address addr = insn.getAddress();
                    Function f = getFunctionContaining(addr);
                    String fname = (f != null) ? f.getName() + "@" + f.getEntryPoint() : "???";
                    println("HIT disp=" + disp + "  load@" + addr + " call@" + cur.getAddress() + "  in " + fname + "   [" + rep + "]  ->  [" + crep + "]");
                    hits++;
                    break;
                }
                if (cur.getMnemonicString().equalsIgnoreCase("CALL")) break; // different call, stop looking
            }
        }
        println("Total hits: " + hits);
    }
}
