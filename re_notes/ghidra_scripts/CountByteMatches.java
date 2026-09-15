// CountByteMatches.java -- counts every occurrence of a raw hex byte pattern (no
// wildcards) across all readable memory blocks, and lists every match address (up
// to a cap). Purpose-built for verifying a candidate signature's real uniqueness
// BEFORE shipping it in a fix -- a signature that matches more than once could hook
// the wrong call site, or (with return-address scoping) simply never fire at all.
//
// Usage: -postScript CountByteMatches.java <output_path> <hexBytesSpaceSeparated>

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;

import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.List;

public class CountByteMatches extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args == null || args.length < 2) {
            println("Usage: CountByteMatches.java <output_path> <hexBytesSpaceSeparated>");
            return;
        }
        String outPath = args[0];
        String[] hexParts = args[1].split("\\s+");
        byte[] needle = new byte[hexParts.length];
        for (int i = 0; i < hexParts.length; i++) {
            needle[i] = (byte) Integer.parseInt(hexParts[i], 16);
        }

        Memory mem = currentProgram.getMemory();
        List<Address> matches = new ArrayList<>();

        for (MemoryBlock block : mem.getBlocks()) {
            if (!block.isInitialized() || !block.isRead()) continue;
            byte[] data;
            try {
                data = new byte[(int) block.getSize()];
                block.getBytes(block.getStart(), data);
            } catch (Exception e) {
                continue;
            }
            outer:
            for (int i = 0; i <= data.length - needle.length; i++) {
                for (int j = 0; j < needle.length; j++) {
                    if (data[i + j] != needle[j]) continue outer;
                }
                matches.add(block.getStart().add(i));
                if (matches.size() > 200) break;
            }
        }

        try (PrintWriter w = new PrintWriter(new FileWriter(outPath))) {
            w.println("Pattern: " + args[1]);
            w.println("Total matches: " + matches.size() + (matches.size() > 200 ? " (capped at 200)" : ""));
            for (Address a : matches) {
                w.println("  " + a);
            }
        }
        println("Wrote report to " + outPath);
    }
}
