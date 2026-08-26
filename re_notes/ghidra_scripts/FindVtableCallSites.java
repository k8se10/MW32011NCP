// FindVtableCallSites.java -- scans the whole .text for indirect CALL instructions
// whose memory operand has a specific displacement (a D3D9 vtable slot offset,
// e.g. 0xbc = SetViewport, 0xc0 = GetViewport for IDirect3DDevice9 on Win32/COM),
// since iw5sp.exe calls these through a vtable pointer, not a resolvable symbol.
// Args (via -scriptArguments in analyzeHeadless): one or more hex displacement
// values, e.g. "0xbc 0xc0". Prints containing function name/address for each hit.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.listing.Function;
import ghidra.program.model.address.Address;

import java.util.HashSet;
import java.util.Set;

public class FindVtableCallSites extends GhidraScript {
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
        println("Scanning for indirect CALL instructions with displacement in: " + targets);

        InstructionIterator it = currentProgram.getListing().getInstructions(true);
        int hits = 0;
        while (it.hasNext() && !monitor.isCancelled()) {
            Instruction insn = it.next();
            String mnem = insn.getMnemonicString();
            if (!mnem.equalsIgnoreCase("CALL")) continue;
            String rep = insn.toString();
            String lower = rep.toLowerCase();
            // must be a memory-indirect call, e.g. "CALL dword ptr [EAX + 0xbc]"
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
