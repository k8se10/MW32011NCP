// RawByteScan.java -- scans every readable memory block for an exact byte sequence,
// independent of whether Ghidra has defined a String data object there (works even
// under -noanalysis, unlike RawStringScan/FindExactStrings which rely on pre-detected
// string data). Use when a known ASCII constant (a magic/signature) isn't turning up
// via the string-based scanners -- e.g. a non-null-terminated fixed-width magic like
// "IWffu100" compared inline rather than treated as a C string.
//
// Usage: -postScript RawByteScan.java <output_path> <asciiNeedle>
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;

import java.io.FileWriter;
import java.io.PrintWriter;
import java.nio.charset.StandardCharsets;

public class RawByteScan extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args == null || args.length < 2) {
            println("Usage: RawByteScan.java <output_path> <asciiNeedle>");
            return;
        }
        String outPath = args[0];
        byte[] needle = args[1].getBytes(StandardCharsets.US_ASCII);

        Memory mem = currentProgram.getMemory();
        FunctionManager fm = currentProgram.getFunctionManager();

        try (PrintWriter w = new PrintWriter(new FileWriter(outPath))) {
            int totalMatches = 0;
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
                    Address matchAddr = block.getStart().add(i);
                    Function f = fm.getFunctionContaining(matchAddr);
                    w.println(matchAddr + " in block " + block.getName()
                        + (f != null ? " (inside function " + f.getName() + ")" : " (data)"));
                    totalMatches++;
                }
            }
            w.println("Total matches: " + totalMatches);
        }
        println("Wrote report to " + outPath);
    }
}
