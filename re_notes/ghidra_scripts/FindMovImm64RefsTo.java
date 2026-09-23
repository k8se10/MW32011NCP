// FindMovImm64RefsTo.java -- raw-byte-scans for "MOV reg64, imm64" (REX.W B8+reg, 10 bytes
// total) whose immediate equals a given address. Complements FindLeaRefsTo.java for cases
// where a table base is loaded via absolute immediate rather than RIP-relative LEA.
// Usage: -postScript FindMovImm64RefsTo.java <output_path> <targetAddr>
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;

import java.io.FileWriter;
import java.io.PrintWriter;

public class FindMovImm64RefsTo extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args == null || args.length < 2) {
            println("Usage: FindMovImm64RefsTo.java <output_path> <targetAddr>");
            return;
        }
        String outPath = args[0];
        Address targetAddr = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(args[1]);
        long targetVal = targetAddr.getOffset();
        Memory mem = currentProgram.getMemory();
        FunctionManager fm = currentProgram.getFunctionManager();

        try (PrintWriter w = new PrintWriter(new FileWriter(outPath))) {
            w.println("Searching for MOV reg64,imm64 instructions loading 0x" + Long.toHexString(targetVal));
            for (MemoryBlock block : mem.getBlocks()) {
                if (!block.isExecute()) continue;
                long size = block.getSize();
                byte[] buf = new byte[(int) Math.min(size, Integer.MAX_VALUE - 16)];
                block.getBytes(block.getStart(), buf);
                int foundCount = 0;
                for (int i = 0; i + 10 <= buf.length; i++) {
                    int rex = buf[i] & 0xFF;
                    if ((rex & 0xF0) != 0x40) continue; // any REX prefix
                    if ((rex & 0x08) == 0) continue;    // must have REX.W set (64-bit operand)
                    int opcode = buf[i + 1] & 0xFF;
                    if (opcode < 0xB8 || opcode > 0xBF) continue; // MOV r64, imm64
                    long imm = 0;
                    for (int b = 7; b >= 0; b--) {
                        imm = (imm << 8) | (buf[i + 2 + b] & 0xFFL);
                    }
                    if (imm == targetVal) {
                        Address insnAddr = block.getStart().add(i);
                        Function f = fm.getFunctionContaining(insnAddr);
                        w.println("MATCH: MOV imm64 at " + insnAddr + " -> 0x" + Long.toHexString(imm)
                                + "  (containing function: " + (f != null ? f.getName() + "@" + f.getEntryPoint() : "NONE/unresolved") + ")");
                        foundCount++;
                    }
                }
                w.println("Block " + block.getName() + ": " + foundCount + " matches.");
            }
        }
        println("Wrote report to " + outPath);
    }
}
