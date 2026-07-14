// ControllerStringCheck.java — targeted check on the handful of controller-sounding
// strings found in the very first strings pass of this project (attachedcontrollercount,
// splitscreenactivegamepadcount, etc). For each, finds its address(es), then walks
// references TO it (function-contained AND data-table-indirect, same technique as
// DescribeRefs.java) to determine whether it's live/reachable code or dead/orphaned data.
//
// Usage: -postScript ControllerStringCheck.java <output_path>

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.DataIterator;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.symbol.ReferenceManager;

import java.io.FileWriter;
import java.io.PrintWriter;

public class ControllerStringCheck extends GhidraScript {

    static final String[] TARGETS = {
        "attachedcontrollercount",
        "splitscreenactivegamepadcount",
        "getsplitscreencontrollerclientnum",
        "requiring live signin on an invalid controller index",
        "requiring signin on an invalid controller index",
        "setct-hodinput",
        "idirectinputjoyconfig_acquire",
        "@platform_usecontroller1",
        "virtual array controller messed up",
    };

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        String outPath = (args != null && args.length > 0) ? args[0] : "D:\\Tools\\ghidra_projects\\controller_check.txt";

        Listing listing = currentProgram.getListing();
        FunctionManager fm = currentProgram.getFunctionManager();
        ReferenceManager refMgr = currentProgram.getReferenceManager();

        try (PrintWriter w = new PrintWriter(new FileWriter(outPath))) {
            DataIterator it = listing.getDefinedData(true);
            while (it.hasNext()) {
                Data data = it.next();
                if (!data.hasStringValue()) continue;
                Object val;
                try { val = data.getValue(); } catch (Exception e) { continue; }
                if (val == null) continue;
                String sval = val.toString();
                String lower = sval.toLowerCase();

                for (String target : TARGETS) {
                    if (lower.contains(target)) {
                        Address strAddr = data.getAddress();
                        w.println("=== '" + sval + "' @ " + strAddr + " ===");
                        ReferenceIterator refs = refMgr.getReferencesTo(strAddr);
                        int count = 0;
                        while (refs.hasNext()) {
                            Reference ref = refs.next();
                            count++;
                            Address from = ref.getFromAddress();
                            Function func = fm.getFunctionContaining(from);
                            if (func != null) {
                                w.println("  from " + from + " IN FUNCTION " + func.getName() + "@" + func.getEntryPoint());
                            } else {
                                w.println("  from " + from + " IN DATA");
                                Data containing = listing.getDataContaining(from);
                                if (containing != null) {
                                    ReferenceIterator refs2 = refMgr.getReferencesTo(containing.getAddress());
                                    while (refs2.hasNext()) {
                                        Reference ref2 = refs2.next();
                                        Address from2 = ref2.getFromAddress();
                                        Function func2 = fm.getFunctionContaining(from2);
                                        w.println("      <- table/data referenced from " + from2
                                            + (func2 != null ? " IN FUNCTION " + func2.getName() + "@" + func2.getEntryPoint() : " IN DATA (unresolved further)"));
                                    }
                                }
                            }
                        }
                        w.println("  (" + count + " direct references)");
                        w.println();
                        break;
                    }
                }
            }
        }
        println("Wrote report to " + outPath);
    }
}
