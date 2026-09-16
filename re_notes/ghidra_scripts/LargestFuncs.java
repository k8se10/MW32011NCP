// LargestFuncs.java -- lists the N largest defined functions in the program by real
// body size (Function.getBody().getNumAddresses(), i.e. actual code bytes spanned,
// not just entry-to-next-entry distance which can be thrown off by gaps/padding).
// Written 2026-09-16 for the GSC-VM interpreter search: a real bytecode interpreter
// needs a large opcode-dispatch switch (likely covering 100+ distinct opcodes), so
// it should be one of the largest functions in the whole binary by raw size --
// this is a genuine, reusable search technique for any future "find the big dispatch
// loop" RE task, not a one-off.
//
// Usage: -postScript LargestFuncs.java <output_path> <topN> [minAddr] [maxAddr]
// minAddr/maxAddr optionally restrict the scan to one address range (hex, no 0x
// prefix) -- useful to scope to just the main .text region and skip vendored/
// third-party code if the binary has any.

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.FunctionManager;

import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.List;

public class LargestFuncs extends GhidraScript {

    private static class Entry {
        Address addr;
        String name;
        long size;
    }

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args == null || args.length < 2) {
            println("Usage: LargestFuncs.java <output_path> <topN> [minAddr] [maxAddr]");
            return;
        }
        String outPath = args[0];
        int topN = Integer.parseInt(args[1]);

        Address minAddr = null, maxAddr = null;
        if (args.length >= 4) {
            minAddr = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(args[2]);
            maxAddr = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(args[3]);
        }

        FunctionManager fm = currentProgram.getFunctionManager();
        FunctionIterator it = fm.getFunctions(true);

        List<Entry> entries = new ArrayList<>();
        while (it.hasNext()) {
            Function f = it.next();
            Address entry = f.getEntryPoint();
            if (minAddr != null && (entry.compareTo(minAddr) < 0 || entry.compareTo(maxAddr) > 0)) {
                continue;
            }
            Entry e = new Entry();
            e.addr = entry;
            e.name = f.getName();
            e.size = f.getBody().getNumAddresses();
            entries.add(e);
        }

        entries.sort(Comparator.comparingLong((Entry e) -> e.size).reversed());

        try (PrintWriter w = new PrintWriter(new FileWriter(outPath))) {
            w.println("Top " + topN + " largest functions by real body size:");
            int count = 0;
            for (Entry e : entries) {
                if (count >= topN) break;
                w.println(String.format("  %s  size=0x%x (%d bytes)  %s", e.addr, e.size, e.size, e.name));
                count++;
            }
        }
        println("Wrote report to " + outPath);
    }
}
