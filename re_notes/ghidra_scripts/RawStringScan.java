// RawStringScan.java — scans raw program memory bytes (not just Ghidra-defined string
// data) for an exact null-terminated ASCII string, then lists every code reference to
// each match. Finds strings Ghidra's own data-type analysis never recognized (common
// when running with -noanalysis), unlike FindStringRefs.java which only searches
// already-defined string data.
//
// Usage: -postScript RawStringScan.java <output_path> <exactString>

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSetView;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.symbol.ReferenceManager;

import java.io.FileWriter;
import java.io.PrintWriter;

public class RawStringScan extends GhidraScript {

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args == null || args.length < 2) {
            println("Usage: RawStringScan.java <output_path> <exactString>");
            return;
        }
        String outPath = args[0];
        String target = args[1];
        byte[] needle = (target + "\0").getBytes("US-ASCII");

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
                for (int i = 0; i <= data.length - needle.length; i++) {
                    boolean match = true;
                    for (int j = 0; j < needle.length; j++) {
                        if (data[i + j] != needle[j]) { match = false; break; }
                    }
                    if (!match) continue;
                    // require preceding byte to be non-alphanumeric/non-underscore (token boundary)
                    if (i > 0) {
                        byte prev = data[i - 1];
                        if ((prev >= '0' && prev <= '9') || (prev >= 'A' && prev <= 'Z')
                            || (prev >= 'a' && prev <= 'z') || prev == '_') continue;
                    }
                    Address at = block.getStart().add(i);
                    w.println("STRING @ " + at + " = \"" + target + "\" (block " + block.getName() + ")");
                    ReferenceIterator refs = refMgr.getReferencesTo(at);
                    int count = 0;
                    while (refs.hasNext()) {
                        Reference ref = refs.next();
                        count++;
                        Address from = ref.getFromAddress();
                        Function func = fm.getFunctionContaining(from);
                        w.println("  from " + from + " type=" + ref.getReferenceType()
                            + (func != null ? " IN FUNCTION " + func.getName() + "@" + func.getEntryPoint()
                                            : " IN DATA/UNKNOWN"));
                    }
                    w.println("  (" + count + " references)");
                    w.println();
                }
            }
        }
        println("Wrote report to " + outPath);
    }
}
