// DescribeRefs.java — Ghidra headless GhidraScript.
// For each given hex address, lists every reference TO it with full detail: from-address,
// reference type, whether the from-address falls inside a function or bare data, and if
// data, that data's own containing structure info. Used to trace indirect (table-driven)
// references that FindInputRefs/FindGlobalRefs miss because they only record function-
// contained references.
//
// Usage: -postScript DescribeRefs.java <output_path> <addr1> [<addr2> ...]

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.symbol.ReferenceManager;

import java.io.FileWriter;
import java.io.PrintWriter;

public class DescribeRefs extends GhidraScript {

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args == null || args.length < 2) {
            println("Usage: DescribeRefs.java <output_path> <addr1> ...");
            return;
        }
        String outPath = args[0];

        FunctionManager fm = currentProgram.getFunctionManager();
        ReferenceManager refMgr = currentProgram.getReferenceManager();
        Listing listing = currentProgram.getListing();

        try (PrintWriter w = new PrintWriter(new FileWriter(outPath))) {
            for (int i = 1; i < args.length; i++) {
                Address target = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(args[i]);
                w.println("=== References TO " + target + " ===");
                ReferenceIterator refs = refMgr.getReferencesTo(target);
                int count = 0;
                while (refs.hasNext()) {
                    Reference ref = refs.next();
                    count++;
                    Address from = ref.getFromAddress();
                    Function func = fm.getFunctionContaining(from);
                    w.println("  from " + from + " type=" + ref.getReferenceType()
                        + (func != null ? " IN FUNCTION " + func.getName() + "@" + func.getEntryPoint()
                                        : " IN DATA"));
                    if (func == null) {
                        Data d = listing.getDataContaining(from);
                        if (d != null) {
                            w.println("    containing data: " + d.getAddress() + " type=" + d.getDataType().getName()
                                + " label=" + (d.getLabel() != null ? d.getLabel() : "(none)"));
                            // one level up: who references this data slot's containing data?
                            ReferenceIterator refs2 = refMgr.getReferencesTo(d.getAddress());
                            while (refs2.hasNext()) {
                                Reference ref2 = refs2.next();
                                Address from2 = ref2.getFromAddress();
                                Function func2 = fm.getFunctionContaining(from2);
                                w.println("      <- referenced from " + from2
                                    + (func2 != null ? " IN FUNCTION " + func2.getName() + "@" + func2.getEntryPoint()
                                                     : " IN DATA"));
                            }
                        }
                    }
                }
                w.println("  (" + count + " total references)");
                w.println();
            }
        }
        println("Wrote report to " + outPath);
    }
}
