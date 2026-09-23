// RawByteSearch.java -- searches memory for a hex byte pattern (space-separated, no
// wildcards) and lists every match address. Doesn't require prior analysis/strings.
// Usage: -postScript RawByteSearch.java <output_path> <hexPattern>
// hexPattern example: "40 53 48 83 EC 30"
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.mem.Memory;
import ghidra.util.task.TaskMonitor;

import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.List;

public class RawByteSearch extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args == null || args.length < 2) {
            println("Usage: RawByteSearch.java <output_path> <hexPattern>");
            return;
        }
        String outPath = args[0];
        String[] hexToks = args[1].trim().split("\\s+");
        byte[] pattern = new byte[hexToks.length];
        for (int i = 0; i < hexToks.length; i++) {
            pattern[i] = (byte) Integer.parseInt(hexToks[i], 16);
        }
        Memory mem = currentProgram.getMemory();
        try (PrintWriter w = new PrintWriter(new FileWriter(outPath))) {
            w.println("Pattern: " + args[1]);
            List<Address> hits = new ArrayList<>();
            Address start = currentProgram.getMinAddress();
            Address found = mem.findBytes(start, pattern, null, true, TaskMonitor.DUMMY);
            int safety = 0;
            while (found != null && safety < 20) {
                hits.add(found);
                safety++;
                found = mem.findBytes(found.add(1), pattern, null, true, TaskMonitor.DUMMY);
            }
            w.println("Hits: " + hits.size());
            for (Address a : hits) {
                w.println("  MATCH @ " + a);
            }
        }
    }
}
