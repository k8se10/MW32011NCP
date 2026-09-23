// FindStringXrefs.java -- finds defined strings matching a substring (case-insensitive) and
// lists every function that references each one.
// Usage: -postScript FindStringXrefs.java <output_path> <substring1> [substring2 ...]
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

public class FindStringXrefs extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args == null || args.length < 2) {
            println("Usage: FindStringXrefs.java <output_path> <substring1> [substring2 ...]");
            return;
        }
        String outPath = args[0];
        FunctionManager fm = currentProgram.getFunctionManager();
        ReferenceManager refMgr = currentProgram.getReferenceManager();

        try (PrintWriter w = new PrintWriter(new FileWriter(outPath))) {
            DataIterator strIt = currentProgram.getListing().getDefinedData(true);
            while (strIt.hasNext()) {
                Data d = strIt.next();
                if (!d.hasStringValue()) continue;
                String val = d.getDefaultValueRepresentation();
                for (int i = 1; i < args.length; i++) {
                    if (val.toLowerCase().contains(args[i].toLowerCase())) {
                        Address strAddr = d.getAddress();
                        w.println("STRING @ " + strAddr + ": " + val);
                        ReferenceIterator refs = refMgr.getReferencesTo(strAddr);
                        int count = 0;
                        while (refs.hasNext()) {
                            Reference r = refs.next();
                            Address fromAddr = r.getFromAddress();
                            Function fn = fm.getFunctionContaining(fromAddr);
                            count++;
                            w.println("  REF #" + count + ": from " + fromAddr + " in " +
                                (fn == null ? "<none/data>" : fn.getName() + " @ " + fn.getEntryPoint()));
                        }
                        w.println();
                        break;
                    }
                }
            }
        }
    }
}
