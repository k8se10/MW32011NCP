// WildcardByteSearch.java -- searches memory for a hex byte pattern with "??" wildcards
// (space-separated, same syntax this project's own SigScan::FindPatternInMainModule uses)
// using Ghidra's native masked findBytes (fast). Usage:
// -postScript WildcardByteSearch.java <output_path> <hexPattern>
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.mem.Memory;
import ghidra.util.task.TaskMonitor;

import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.List;

public class WildcardByteSearch extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args == null || args.length < 2) {
            println("Usage: WildcardByteSearch.java <output_path> <hexPattern>");
            return;
        }
        String outPath = args[0];
        String[] toks = args[1].trim().split("\\s+");
        int n = toks.length;
        byte[] pattern = new byte[n];
        byte[] mask = new byte[n];
        for (int i = 0; i < n; i++) {
            if (toks[i].equals("??")) { mask[i] = 0x00; continue; }
            pattern[i] = (byte) Integer.parseInt(toks[i], 16);
            mask[i] = (byte) 0xFF;
        }
        Memory mem = currentProgram.getMemory();
        try (PrintWriter w = new PrintWriter(new FileWriter(outPath))) {
            w.println("Pattern (" + n + " bytes): " + args[1]);
            List<Address> hits = new ArrayList<>();
            Address start = currentProgram.getMinAddress();
            Address found = mem.findBytes(start, pattern, mask, true, TaskMonitor.DUMMY);
            int safety = 0;
            while (found != null && safety < 20) {
                hits.add(found);
                safety++;
                found = mem.findBytes(found.add(1), pattern, mask, true, TaskMonitor.DUMMY);
            }
            w.println("Hits: " + hits.size());
            for (Address a : hits) {
                w.println("  MATCH @ " + a);
            }
        }
    }
}
