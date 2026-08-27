// FindCallersByName.java — like FindCallers.java but resolves the target by exact symbol
// name (works for EXTERNAL/import symbols too, whose addresses live outside the default
// RAM address space and can't be parsed by getAddress(String)).
//
// Usage: -postScript FindCallersByName.java <output_path> <symbolName>

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.symbol.ReferenceManager;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolIterator;
import ghidra.program.model.symbol.SymbolTable;

import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.LinkedHashSet;
import java.util.Set;

public class FindCallersByName extends GhidraScript {

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args == null || args.length < 2) {
            println("Usage: FindCallersByName.java <output_path> <symbolName>");
            return;
        }
        String outPath = args[0];
        String name = args[1];

        SymbolTable st = currentProgram.getSymbolTable();
        Address target = null;
        SymbolIterator it = st.getSymbolIterator();
        while (it.hasNext()) {
            Symbol s = it.next();
            if (s.getName().equals(name)) {
                target = s.getAddress();
                break;
            }
        }

        try (PrintWriter w = new PrintWriter(new FileWriter(outPath))) {
            if (target == null) {
                w.println("Symbol not found: " + name);
                println("Symbol not found: " + name);
                return;
            }
            w.println("Target symbol: " + name + " @ " + target);

            FunctionManager fm = currentProgram.getFunctionManager();
            ReferenceManager refMgr = currentProgram.getReferenceManager();

            Set<Function> callers = new LinkedHashSet<>();
            ReferenceIterator refs = refMgr.getReferencesTo(target);
            while (refs.hasNext()) {
                Reference ref = refs.next();
                Function f = fm.getFunctionContaining(ref.getFromAddress());
                if (f != null) callers.add(f);
            }

            DecompInterface decomp = new DecompInterface();
            decomp.openProgram(currentProgram);
            decomp.setSimplificationStyle("decompile");

            w.println("Callers (" + callers.size() + "):");
            for (Function f : callers) {
                w.println("  " + f.getName() + " @ " + f.getEntryPoint());
            }
            w.println();
            for (Function f : callers) {
                w.println("================================================================");
                w.println("Function: " + f.getName() + " @ " + f.getEntryPoint());
                w.println("----------------------------------------------------------------");
                DecompileResults results = decomp.decompileFunction(f, 60, monitor);
                if (results != null && results.decompileCompleted()) {
                    w.println(results.getDecompiledFunction().getC());
                } else {
                    w.println("DECOMPILE FAILED");
                }
                w.println();
            }
            decomp.dispose();
        }
        println("Wrote report to " + outPath);
    }
}
