// FindExactStrings.java -- like FindStringRefs.java but EXACT match (not substring),
// for short keyword-table strings like "rect"/"forecolor" where a substring search
// drowns in unrelated matches ("direct", "correct", "directory", ...).
// Usage: -postScript FindExactStrings.java <output_path> <exact1> [exact2 ...]
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.DataIterator;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.symbol.ReferenceManager;

import java.io.FileWriter;
import java.io.PrintWriter;

public class FindExactStrings extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args == null || args.length < 2) {
            println("Usage: FindExactStrings.java <output_path> <exact1> ...");
            return;
        }
        String outPath = args[0];
        FunctionManager fm = currentProgram.getFunctionManager();
        ReferenceManager refMgr = currentProgram.getReferenceManager();

        try (PrintWriter w = new PrintWriter(new FileWriter(outPath))) {
            DataIterator it = currentProgram.getListing().getDefinedData(true);
            int matches = 0;
            while (it.hasNext()) {
                Data d = it.next();
                if (!d.hasStringValue()) continue;
                Object val = d.getValue();
                if (val == null) continue;
                String s = val.toString();
                for (int i = 1; i < args.length; i++) {
                    if (s.equals(args[i])) {
                        matches++;
                        Address at = d.getAddress();
                        w.println("STRING @ " + at + " = \"" + s + "\"");
                        ReferenceIterator refs = refMgr.getReferencesTo(at);
                        int count = 0;
                        while (refs.hasNext()) {
                            Reference ref = refs.next();
                            count++;
                            Address from = ref.getFromAddress();
                            Function func = fm.getFunctionContaining(from);
                            w.println("  from " + from + " type=" + ref.getReferenceType()
                                + (func != null ? " IN FUNCTION " + func.getName() + "@" + func.getEntryPoint()
                                                : " IN DATA"));
                        }
                        w.println("  (" + count + " references)");
                        w.println();
                        break;
                    }
                }
            }
            w.println("Total matching strings: " + matches);
        }
        println("Wrote report to " + outPath);
    }
}
