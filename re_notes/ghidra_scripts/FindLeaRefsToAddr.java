// FindLeaRefsToAddr.java -- raw byte-level scan for x86-64 LEA reg,[RIP+disp32]
// instructions whose computed target equals a given address, regardless of
// whether Ghidra's own disassembler/analyzer ever visited that instruction or
// created a reference for it. Works directly on memory bytes, not the
// instruction/reference database -- the last-resort technique when a target is
// confirmed to have ZERO xrefs after full analysis (this project's own
// documented indirect-reference tooling gap).
//
// LEA reg64/32, [rip+disp32] encodes as: [REX.W?] 8D /r (ModRM: mod=00, rm=101)
// followed by a 4-byte little-endian signed displacement. Effective target =
// address-of-next-instruction + disp32.
//
// Usage: -postScript FindLeaRefsToAddr.java <output_path> <targetAddrHex>
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSetView;
import ghidra.program.model.listing.Function;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;

import java.io.FileWriter;
import java.io.PrintWriter;

public class FindLeaRefsToAddr extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args == null || args.length < 2) {
            println("Usage: FindLeaRefsToAddr.java <output_path> <targetAddrHex>");
            return;
        }
        String outPath = args[0];
        long target = Long.parseUnsignedLong(args[1].replace("0x", ""), 16);
        Memory mem = currentProgram.getMemory();

        try (PrintWriter w = new PrintWriter(new FileWriter(outPath))) {
            w.println("Scanning for LEA reg,[rip+disp32] -> 0x" + Long.toHexString(target));
            int hits = 0;
            for (MemoryBlock block : mem.getBlocks()) {
                if (!block.isInitialized()) continue;
                Address start = block.getStart();
                Address end = block.getEnd();
                long size = block.getSize();
                byte[] buf;
                try {
                    buf = new byte[(int) size];
                    block.getBytes(start, buf);
                } catch (Exception e) {
                    w.println("  (failed to read block " + block.getName() + ": " + e.getMessage() + ")");
                    continue;
                }
                for (int i = 0; i + 6 <= buf.length; i++) {
                    // Optional REX prefix (0x40-0x4F), then 0x8D, then ModRM with mod=00,rm=101
                    int off = i;
                    boolean hasRex = (buf[off] & 0xF0) == 0x40;
                    int opcodeIdx = hasRex ? off + 1 : off;
                    if (opcodeIdx + 5 >= buf.length) continue;
                    if ((buf[opcodeIdx] & 0xFF) != 0x8D) continue;
                    int modrm = buf[opcodeIdx + 1] & 0xFF;
                    int mod = (modrm >> 6) & 0x3;
                    int rm = modrm & 0x7;
                    if (mod != 0 || rm != 5) continue; // not [rip+disp32]
                    int dispStart = opcodeIdx + 2;
                    if (dispStart + 4 > buf.length) continue;
                    int disp = (buf[dispStart] & 0xFF) | ((buf[dispStart + 1] & 0xFF) << 8)
                            | ((buf[dispStart + 2] & 0xFF) << 16) | ((buf[dispStart + 3] & 0xFF) << 24);
                    long insnAddr = start.getOffset() + off;
                    long nextInsnAddr = start.getOffset() + dispStart + 4;
                    long computedTarget = nextInsnAddr + disp;
                    if (computedTarget == target) {
                        Address a = start.getNewAddress(insnAddr);
                        Function f = getFunctionContaining(a);
                        String fname = (f != null) ? f.getName() + "@" + f.getEntryPoint() : "??? (no function defined here)";
                        w.println("  LEA HIT @ 0x" + Long.toHexString(insnAddr) + " in " + fname
                                + "  bytes=" + bytesToHex(buf, off, opcodeIdx + 6 - off));
                        hits++;
                    }
                }
                // Also scan for a raw 8-byte absolute pointer equal to target (either a
                // MOV r64,imm64 immediate operand, or a plain data-section pointer table
                // entry -- both are 8 raw LE bytes regardless of which).
                for (int i = 0; i + 8 <= buf.length; i++) {
                    long val = 0;
                    for (int b = 7; b >= 0; b--) {
                        val = (val << 8) | (buf[i + b] & 0xFFL);
                    }
                    if (val == target) {
                        long absAddr = start.getOffset() + i;
                        Address a = start.getNewAddress(absAddr);
                        Function f = getFunctionContaining(a);
                        String fname = (f != null) ? f.getName() + "@" + f.getEntryPoint() : "(data, block " + block.getName() + ")";
                        w.println("  ABS64 HIT @ 0x" + Long.toHexString(absAddr) + " in " + fname);
                        hits++;
                    }
                }
            }
            w.println("Total hits: " + hits);
        }
        println("Wrote report to " + outPath);
    }

    private static String bytesToHex(byte[] buf, int start, int len) {
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < len && start + i < buf.length; i++) {
            sb.append(String.format("%02X ", buf[start + i]));
        }
        return sb.toString().trim();
    }
}
