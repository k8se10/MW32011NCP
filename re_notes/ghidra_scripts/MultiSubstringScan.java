// MultiSubstringScan.java -- like MultiStringScan.java but matches the needle as a
// PREFIX (token-boundary before it, no requirement on what follows) rather than a
// whole null-terminated string. Finds compound identifiers like "ac130_105mm_sp"
// when searching for "105mm" or "ac130".
//
// Usage: -postScript MultiSubstringScan.java <output_path> <needle1> [needle2 ...]

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.symbol.ReferenceManager;

import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.List;

public class MultiSubstringScan extends GhidraScript {

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args == null || args.length < 2) {
            println("Usage: MultiSubstringScan.java <output_path> <needle1> [needle2 ...]");
            return;
        }
        String outPath = args[0];
        List<byte[]> needles = new ArrayList<>();
        List<String> labels = new ArrayList<>();
        for (int i = 1; i < args.length; i++) {
            needles.add(args[i].getBytes("US-ASCII"));
            labels.add(args[i]);
        }

        Memory mem = currentProgram.getMemory();
        FunctionManager fm = currentProgram.getFunctionManager();
        ReferenceManager refMgr = currentProgram.getReferenceManager();

        try (PrintWriter w = new PrintWriter(new FileWriter(outPath))) {
            for (MemoryBlock block : mem.getBlocks()) {
                if (!block.isInitialized()) continue;
                byte[] data;
                try {
                    data = new byte[(int) block.getSize()];
                    block.getBytes(block.getStart(), data);
                } catch (Exception e) {
                    continue;
                }
                for (int n = 0; n < needles.size(); n++) {
                    byte[] needle = needles.get(n);
                    String label = labels.get(n);
                    for (int i = 0; i <= data.length - needle.length; i++) {
                        boolean match = true;
                        for (int j = 0; j < needle.length; j++) {
                            if (data[i + j] != needle[j]) { match = false; break; }
                        }
                        if (!match) continue;
                        if (i > 0) {
                            byte prev = data[i - 1];
                            if ((prev >= '0' && prev <= '9') || (prev >= 'A' && prev <= 'Z')
                                || (prev >= 'a' && prev <= 'z') || prev == '_') continue;
                        }
                        // extract the full printable-ASCII run containing this match, for context
                        int start = i;
                        while (start > 0) {
                            byte b = data[start - 1];
                            if (b >= 0x20 && b < 0x7f) start--; else break;
                        }
                        int end = i + needle.length;
                        while (end < data.length) {
                            byte b = data[end];
                            if (b >= 0x20 && b < 0x7f) end++; else break;
                        }
                        String full = new String(data, start, end - start, "US-ASCII");
                        Address at = block.getStart().add(i);
                        w.println("MATCH \"" + label + "\" @ " + at + " (full string: \"" + full + "\")");
                        ReferenceIterator refs = refMgr.getReferencesTo(block.getStart().add(start));
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
                        w.println("  (" + count + " references to string start)");
                    }
                }
            }
            w.flush();
        }
        println("Wrote results to " + outPath);
    }
}
