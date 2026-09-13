// FindSelfRecursiveFuncs.java -- scans every defined function in the program for a
// direct CALL instruction whose target is the function's OWN entry point (genuine
// self-recursion), and reports the self-call count per function.
//
// Written 2026-09-13 for the x64 motion-blur hook-point RE task: x86's
// FUN_00508970 (the exclusion-zone/PIP rect-carving function that sits directly
// between FUN_00694650 and FUN_00497210/FUN_00693ff0 in the frame-composite call
// chain) is a rare, structurally distinctive function -- it calls itself directly
// up to 4 times per invocation. That's a narrow, scriptable search key: almost no
// other function in this binary should have >=2 genuine self-recursive CALL sites.
//
// Usage: -postScript FindSelfRecursiveFuncs.java <output_path> [minSelfCalls]

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.symbol.FlowType;
import ghidra.program.model.symbol.Reference;

import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.List;

public class FindSelfRecursiveFuncs extends GhidraScript {

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args == null || args.length < 1) {
            println("Usage: FindSelfRecursiveFuncs.java <output_path> [minSelfCalls]");
            return;
        }
        String outPath = args[0];
        int minSelfCalls = args.length >= 2 ? Integer.parseInt(args[1]) : 2;

        FunctionManager fm = currentProgram.getFunctionManager();
        Listing listing = currentProgram.getListing();

        List<String> hits = new ArrayList<>();
        FunctionIterator it = fm.getFunctions(true);
        int scanned = 0;
        while (it.hasNext()) {
            Function f = it.next();
            scanned++;
            Address entry = f.getEntryPoint();
            Address end = f.getBody().getMaxAddress();
            InstructionIterator insns = listing.getInstructions(f.getBody(), true);
            int selfCalls = 0;
            while (insns.hasNext()) {
                Instruction insn = insns.next();
                FlowType flow = insn.getFlowType();
                if (!flow.isCall()) continue;
                for (Reference ref : insn.getReferencesFrom()) {
                    if (!ref.getReferenceType().isCall()) continue;
                    if (ref.getToAddress().equals(entry)) {
                        selfCalls++;
                    }
                }
            }
            if (selfCalls >= minSelfCalls) {
                hits.add(f.getName() + " @ " + entry + "  self-calls=" + selfCalls
                        + "  params=" + f.getParameterCount()
                        + "  bodySize=" + f.getBody().getNumAddresses());
            }
            if (monitor.isCancelled()) break;
        }

        try (PrintWriter w = new PrintWriter(new FileWriter(outPath))) {
            w.println("Scanned " + scanned + " functions, minSelfCalls=" + minSelfCalls);
            w.println("Hits: " + hits.size());
            for (String h : hits) w.println("  " + h);
        }
        println("Wrote report to " + outPath + " (" + hits.size() + " hits / " + scanned + " scanned)");
    }
}
