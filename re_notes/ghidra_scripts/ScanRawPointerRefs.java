// ScanRawPointerRefs.java -- scans every initialized/readable memory block for raw
// 4-byte little-endian occurrences of a given target address, i.e. finds function-
// pointer TABLE entries (vtables, callback tables, EV_* dispatch tables) that
// reference a function but aren't a normal call-site Reference Ghidra's own
// cross-reference view would show, and that FindConstantRefs.java (which only
// scans instruction operands) can't see either.
//
// Usage: -postScript ScanRawPointerRefs.java <output_path> <targetAddrHex>

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;

import java.io.FileWriter;
import java.io.PrintWriter;

public class ScanRawPointerRefs extends GhidraScript {

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args == null || args.length < 2) {
            println("Usage: ScanRawPointerRefs.java <output_path> <targetAddrHex>");
            return;
        }
        String outPath = args[0];
        long target = Long.parseLong(args[1], 16) & 0xFFFFFFFFL;
        byte b0 = (byte) (target & 0xFF);
        byte b1 = (byte) ((target >> 8) & 0xFF);
        byte b2 = (byte) ((target >> 16) & 0xFF);
        byte b3 = (byte) ((target >> 24) & 0xFF);

        Memory mem = currentProgram.getMemory();
        int hits = 0;

        try (PrintWriter w = new PrintWriter(new FileWriter(outPath))) {
            for (MemoryBlock block : mem.getBlocks()) {
                if (!block.isInitialized() || !block.isRead()) continue;
                long size = block.getSize();
                if (size > 64L * 1024 * 1024) continue;
                byte[] data = new byte[(int) size];
                try {
                    block.getBytes(block.getStart(), data);
                } catch (Exception e) {
                    continue;
                }
                for (int off = 0; off + 4 <= data.length; off++) {
                    if (data[off] == b0 && data[off + 1] == b1 && data[off + 2] == b2
                            && data[off + 3] == b3) {
                        Address a = block.getStart().add(off);
                        w.println(a + "\t(in block " + block.getName() + ")");
                        hits++;
                    }
                }
            }
            w.println("DONE hits=" + hits);
        }
        println("Wrote " + hits + " raw pointer refs to " + outPath);
    }
}
