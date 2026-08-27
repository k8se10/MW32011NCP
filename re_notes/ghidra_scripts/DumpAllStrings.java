// DumpAllStrings.java — scans raw program memory bytes for every printable ASCII
// run of at least minLen characters, null- or non-terminated, and writes them all
// out one per line with their address. A `strings`-utility equivalent for when
// the real `strings` tool isn't available and Ghidra's own data-type analysis
// wasn't run (-noanalysis).
//
// Usage: -postScript DumpAllStrings.java <output_path> <minLen>

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;

import java.io.FileWriter;
import java.io.PrintWriter;

public class DumpAllStrings extends GhidraScript {

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args == null || args.length < 2) {
            println("Usage: DumpAllStrings.java <output_path> <minLen>");
            return;
        }
        String outPath = args[0];
        int minLen = Integer.parseInt(args[1]);

        Memory mem = currentProgram.getMemory();

        try (PrintWriter w = new PrintWriter(new FileWriter(outPath))) {
            for (MemoryBlock block : mem.getBlocks()) {
                if (!block.isInitialized() || !block.isRead()) continue;
                byte[] data;
                try {
                    long size = block.getSize();
                    if (size > 64L * 1024 * 1024) continue; // skip absurdly large blocks
                    data = new byte[(int) size];
                    block.getBytes(block.getStart(), data);
                } catch (Exception e) {
                    continue;
                }
                StringBuilder cur = new StringBuilder();
                long runStart = 0;
                for (int i = 0; i < data.length; i++) {
                    int b = data[i] & 0xFF;
                    boolean printable = (b >= 0x20 && b < 0x7F);
                    if (printable) {
                        if (cur.length() == 0) runStart = i;
                        cur.append((char) b);
                    } else {
                        if (cur.length() >= minLen) {
                            Address a = block.getStart().add(runStart);
                            w.println(a + "\t" + cur.toString());
                        }
                        cur.setLength(0);
                    }
                }
                if (cur.length() >= minLen) {
                    Address a = block.getStart().add(runStart);
                    w.println(a + "\t" + cur.toString());
                }
            }
            w.println("DONE");
        }
        println("Wrote strings to " + outPath);
    }
}
