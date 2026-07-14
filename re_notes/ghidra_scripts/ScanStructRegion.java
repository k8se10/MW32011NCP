// ScanStructRegion.java — scans every byte address in a given range for references,
// and ranks functions by how many DISTINCT addresses within that range they touch.
// Used to find the real consumer of a big flat client-input state struct once its
// approximate bounds are known from prior findings, without needing individual field
// names/offsets.
//
// Usage: -postScript ScanStructRegion.java <output_path> <startAddr> <endAddr>

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.symbol.ReferenceManager;

import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.*;

public class ScanStructRegion extends GhidraScript {

    static class Hit {
        Function func;
        Set<Long> offsets = new TreeSet<>();
    }

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args == null || args.length < 3) {
            println("Usage: ScanStructRegion.java <output_path> <startAddr> <endAddr>");
            return;
        }
        String outPath = args[0];
        Address start = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(args[1]);
        Address end = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(args[2]);
        long startOff = start.getOffset();
        long endOff = end.getOffset();

        FunctionManager fm = currentProgram.getFunctionManager();
        ReferenceManager refMgr = currentProgram.getReferenceManager();

        Map<Address, Hit> funcHits = new HashMap<>();

        for (long off = startOff; off <= endOff; off++) {
            Address a = start.getAddressSpace().getAddress(off);
            ReferenceIterator refs = refMgr.getReferencesTo(a);
            while (refs.hasNext()) {
                Reference ref = refs.next();
                Address from = ref.getFromAddress();
                Function func = fm.getFunctionContaining(from);
                if (func == null) continue;
                Address key = func.getEntryPoint();
                Hit hit = funcHits.get(key);
                if (hit == null) {
                    hit = new Hit();
                    hit.func = func;
                    funcHits.put(key, hit);
                }
                hit.offsets.add(off - startOff);
            }
        }

        List<Hit> ranked = new ArrayList<>(funcHits.values());
        ranked.sort((a, b) -> b.offsets.size() - a.offsets.size());

        try (PrintWriter w = new PrintWriter(new FileWriter(outPath))) {
            w.println("Region " + start + " - " + end + ", " + ranked.size() + " candidate functions");
            for (Hit hit : ranked) {
                w.println();
                w.println("---- " + hit.func.getName() + " @ " + hit.func.getEntryPoint()
                    + " (" + hit.offsets.size() + " distinct offsets) ----");
                w.println("    offsets (relative to " + start + "): " + hit.offsets);
            }
        }
        println("Wrote report to " + outPath);
    }
}
