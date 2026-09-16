// NearestFuncs.java — lists N functions immediately before and after a given
// address, by entry-point order, to check whether an unfound sibling function
// (e.g. reached only via an indirect call) sits address-adjacent to an
// already-known one. Compilers/linkers often keep functions from the same
// source file contiguous.
//
// Usage: -postScript NearestFuncs.java <output_path> <N> <addr1> [addr2 ...]

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.listing.FunctionIterator;

import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.List;

public class NearestFuncs extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args == null || args.length < 3) {
            println("Usage: NearestFuncs.java <output_path> <N> <addr1> [addr2 ...]");
            return;
        }
        String outPath = args[0];
        int n = Integer.parseInt(args[1]);
        FunctionManager fm = currentProgram.getFunctionManager();

        try (PrintWriter w = new PrintWriter(new FileWriter(outPath))) {
            for (int i = 2; i < args.length; i++) {
                Address addr = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(args[i]);
                w.println("=== Neighbors of " + addr + " ===");

                Function anchor = fm.getFunctionContaining(addr);
                if (anchor == null) {
                    w.println("  (no function contains this address)");
                    w.println();
                    continue;
                }
                w.println("  Anchor: " + anchor.getName() + " @ " + anchor.getEntryPoint()
                    + "  size=0x" + Long.toHexString(anchor.getBody().getNumAddresses()));

                List<Function> before = new ArrayList<>();
                FunctionIterator backIter = fm.getFunctions(anchor.getEntryPoint(), false);
                // First result from a non-forward iterator starting AT anchor's
                // entry is the anchor itself (or the function containing it) --
                // skip until we've passed the anchor's own entry point.
                for (Function f : backIter) {
                    if (f.getEntryPoint().equals(anchor.getEntryPoint())) continue;
                    before.add(0, f);
                    if (before.size() >= n) break;
                }
                for (Function f : before) {
                    w.println("  BEFORE: " + f.getName() + " @ " + f.getEntryPoint()
                        + "  size=0x" + Long.toHexString(f.getBody().getNumAddresses())
                        + "  params=" + f.getParameterCount());
                }

                List<Function> after = new ArrayList<>();
                FunctionIterator fwdIter = fm.getFunctions(anchor.getEntryPoint(), true);
                for (Function f : fwdIter) {
                    if (f.getEntryPoint().equals(anchor.getEntryPoint())) continue;
                    after.add(f);
                    if (after.size() >= n) break;
                }
                for (Function f : after) {
                    w.println("  AFTER:  " + f.getName() + " @ " + f.getEntryPoint()
                        + "  size=0x" + Long.toHexString(f.getBody().getNumAddresses())
                        + "  params=" + f.getParameterCount());
                }
                w.println();
            }
        }
        println("Wrote report to " + outPath);
    }
}
