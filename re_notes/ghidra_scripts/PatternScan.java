// PatternScan.java — scans the program's memory for a byte pattern with '?' wildcard
// bytes (space-separated hex, e.g. "BF ? ? ? ? 7E 2A"), then reads a little-endian
// 4-byte address at (matchAddress + derefOffset), the same "find instruction, read the
// immediate operand baked into it" technique used for the confirmed-real static-address
// leads in this project's own investigation (kbuttons, gate bits, etc.) -- adapted here
// to independently verify candidate offsets from an external BSD-3-licensed reference
// (references/mw3-surviv0r, cloned locally for research only, not redistributed).
//
// Usage: -postScript PatternScan.java <output_path> <label> <pattern> <derefOffsetDec>

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.mem.Memory;

import java.io.FileWriter;
import java.io.PrintWriter;

public class PatternScan extends GhidraScript {

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args == null || args.length < 4) {
            println("Usage: PatternScan.java <output_path> <label> <pattern> <derefOffsetDec>");
            return;
        }
        String outPath = args[0];
        String label = args[1];
        String patternStr = args[2];
        int derefOffset = Integer.parseInt(args[3]);

        String[] tokens = patternStr.trim().split("\\s+");
        byte[] bytes = new byte[tokens.length];
        byte[] masks = new byte[tokens.length];
        for (int i = 0; i < tokens.length; i++) {
            if (tokens[i].equals("?")) {
                bytes[i] = 0;
                masks[i] = 0;
            } else {
                bytes[i] = (byte) Integer.parseInt(tokens[i], 16);
                masks[i] = (byte) 0xFF;
            }
        }

        Memory mem = currentProgram.getMemory();
        Address start = currentProgram.getMinAddress();
        StringBuilder sb = new StringBuilder();
        sb.append("Pattern scan for ").append(label).append(": \"").append(patternStr).append("\"\n");

        Address found = mem.findBytes(start, bytes, masks, true, monitor);
        int hitCount = 0;
        Address searchFrom = start;
        while (found != null && hitCount < 20) {
            hitCount++;
            Address derefAt = found.add(derefOffset);
            long rawLE = 0;
            try {
                byte[] four = new byte[4];
                mem.getBytes(derefAt, four);
                rawLE = (four[0] & 0xFFL) | ((four[1] & 0xFFL) << 8) | ((four[2] & 0xFFL) << 16) | ((four[3] & 0xFFL) << 24);
            } catch (Exception e) {
                sb.append("  match @ ").append(found).append(" -- failed to read deref bytes: ").append(e.getMessage()).append("\n");
                searchFrom = found.add(1);
                found = mem.findBytes(searchFrom, bytes, masks, true, monitor);
                continue;
            }
            sb.append("  match @ ").append(found)
              .append("  ->  deref(+").append(derefOffset).append(") = 0x")
              .append(Long.toHexString(rawLE)).append("\n");
            searchFrom = found.add(1);
            found = mem.findBytes(searchFrom, bytes, masks, true, monitor);
        }
        if (hitCount == 0) sb.append("  NO MATCHES FOUND\n");
        if (hitCount == 20) sb.append("  (stopped after 20 matches -- pattern may be too short/generic)\n");

        try (PrintWriter w = new PrintWriter(new FileWriter(outPath, true))) {
            w.print(sb.toString());
            w.println();
        }
        println(sb.toString());
    }
}
