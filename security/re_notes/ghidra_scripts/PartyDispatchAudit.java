// PartyDispatchAudit.java — Ghidra headless GhidraScript.
// For MW32011NSP's PartyHost/joinParty vulnerability research:
//   1. Searches for the literal strings "joinParty"/"joinparty"/"MemberJoin"/"memberjoin"
//      anywhere in the program (case variants), reporting any hits + their xrefs.
//   2. Decompiles a fixed target function (the known per-client dispatcher,
//      FUN_006bf590) and every function that CALLS it, plus every function that
//      IT calls (one level down), so the full local neighborhood is visible in
//      one report without needing a live GUI session.
//
// Usage: -postScript PartyDispatchAudit.java <output_path> <dispatcherAddr>

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.symbol.ReferenceManager;
import ghidra.program.model.symbol.RefType;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.DataIterator;

import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.LinkedHashSet;
import java.util.Set;

public class PartyDispatchAudit extends GhidraScript {

    private DecompInterface decomp;

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args == null || args.length < 2) {
            println("Usage: PartyDispatchAudit.java <output_path> <dispatcherAddr>");
            return;
        }
        String outPath = args[0];
        Address dispatcherAddr = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(args[1]);

        decomp = new DecompInterface();
        decomp.openProgram(currentProgram);
        decomp.setSimplificationStyle("decompile");

        try (PrintWriter w = new PrintWriter(new FileWriter(outPath))) {

            w.println("=== STRING SEARCH: joinParty / MemberJoin variants ===");
            String[] needles = {"joinparty", "memberjoin", "partyhost", "partyatomichost"};
            DataIterator strIt = currentProgram.getListing().getDefinedData(true);
            int hits = 0;
            while (strIt.hasNext()) {
                Data d = strIt.next();
                if (!d.hasStringValue()) continue;
                String val;
                try {
                    Object v = d.getValue();
                    val = v != null ? v.toString() : null;
                } catch (Exception e) {
                    val = null;
                }
                if (val == null) continue;
                String lower = val.toLowerCase();
                for (String needle : needles) {
                    if (lower.contains(needle)) {
                        hits++;
                        w.println("HIT @ " + d.getAddress() + " : \"" + val + "\"");
                        ReferenceManager rm = currentProgram.getReferenceManager();
                        ReferenceIterator xrefs = rm.getReferencesTo(d.getAddress());
                        while (xrefs.hasNext()) {
                            Reference r = xrefs.next();
                            Function f = currentProgram.getFunctionManager().getFunctionContaining(r.getFromAddress());
                            w.println("    xref from " + r.getFromAddress() + (f != null ? " in " + f.getName() : ""));
                        }
                        break;
                    }
                }
            }
            w.println("Total string hits: " + hits);
            w.println();

            w.println("=== DISPATCHER NEIGHBORHOOD: " + dispatcherAddr + " ===");
            FunctionManager fm = currentProgram.getFunctionManager();
            Function dispatcher = fm.getFunctionContaining(dispatcherAddr);
            if (dispatcher == null) {
                w.println("No function found containing " + dispatcherAddr);
            } else {
                dumpFunc(w, dispatcher, "TARGET DISPATCHER");

                w.println("--- Callers of dispatcher ---");
                Set<Function> callers = new LinkedHashSet<>();
                ReferenceIterator refs = currentProgram.getReferenceManager().getReferencesTo(dispatcher.getEntryPoint());
                while (refs.hasNext()) {
                    Reference ref = refs.next();
                    if (!ref.getReferenceType().isCall()) continue;
                    Function f = fm.getFunctionContaining(ref.getFromAddress());
                    if (f != null) callers.add(f);
                }
                for (Function f : callers) {
                    dumpFunc(w, f, "CALLER");
                }

                w.println("--- Callees of dispatcher (one level) ---");
                Set<Function> callees = new LinkedHashSet<>();
                InstructionIterator instrs = currentProgram.getListing().getInstructions(dispatcher.getBody(), true);
                while (instrs.hasNext()) {
                    Instruction ins = instrs.next();
                    if (!ins.getFlowType().isCall()) continue;
                    for (Reference r : ins.getReferencesFrom()) {
                        Function f = fm.getFunctionAt(r.getToAddress());
                        if (f != null) callees.add(f);
                    }
                }
                for (Function f : callees) {
                    dumpFunc(w, f, "CALLEE");
                }
            }
        }
        decomp.dispose();
        println("Wrote report to " + outPath);
    }

    private void dumpFunc(PrintWriter w, Function f, String role) {
        w.println("================================================================");
        w.println(role + ": " + f.getName() + " @ " + f.getEntryPoint());
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
