// FindQwordValueOccurrences.java -- scans every readable memory block for 8-byte-aligned
// occurrences of a specific qword VALUE (e.g. a string's own address stored as a data
// pointer in some table), rather than a code reference to it. Complements
// FindLeaRefsTo.java (code refs) for cases where a value lives in a data table instead.
// Usage: -postScript FindQwordValueOccurrences.java <output_path> <targetValueHex>
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;

import java.io.FileWriter;
import java.io.PrintWriter;

public class FindQwordValueOccurrences extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args == null || args.length < 2) {
            println("Usage: FindQwordValueOccurrences.java <output_path> <targetValueHex>");
            return;
        }
        String outPath = args[0];
        long targetVal = Long.parseUnsignedLong(args[1], 16);
        Memory mem = currentProgram.getMemory();

        try (PrintWriter w = new PrintWriter(new FileWriter(outPath))) {
            w.println("Searching for qword value 0x" + Long.toHexString(targetVal) + " (8-byte aligned)");
            for (MemoryBlock block : mem.getBlocks()) {
                if (!block.isInitialized() || !block.isRead()) continue;
                long size = block.getSize();
                if (size > 64 * 1024 * 1024) { w.println("Skipping huge block " + block.getName()); continue; }
                byte[] buf = new byte[(int) size];
                block.getBytes(block.getStart(), buf);
                int foundCount = 0;
                for (int i = 0; i + 8 <= buf.length; i += 8) {
                    long val = 0;
                    for (int b = 7; b >= 0; b--) {
                        val = (val << 8) | (buf[i + b] & 0xFFL);
                    }
                    if (val == targetVal) {
                        Address addr = block.getStart().add(i);
                        w.println("MATCH at " + addr);
                        foundCount++;
                    }
                }
                w.println("Block " + block.getName() + " [" + block.getStart() + "-" + block.getEnd() + "]: " + foundCount + " matches.");
            }
        }
        println("Wrote report to " + outPath);
    }
}
