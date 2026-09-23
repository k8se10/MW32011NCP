// FindLeaRefsTo.java -- raw-byte-scans every executable block for "LEA reg, [rip+disp32]"
// (REX.W 48 8D /r modrm=00 reg 101, i.e. mod=00 rm=101) whose computed target equals a
// given address. Works without a full analysis pass (pure byte pattern + arithmetic),
// same class of technique as FindImportSlotAndCallers.java but for data refs, not calls.
// Usage: -postScript FindLeaRefsTo.java <output_path> <targetAddr>
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;

import java.io.FileWriter;
import java.io.PrintWriter;

public class FindLeaRefsTo extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args == null || args.length < 2) {
            println("Usage: FindLeaRefsTo.java <output_path> <targetAddr>");
            return;
        }
        String outPath = args[0];
        Address targetAddr = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(args[1]);
        Memory mem = currentProgram.getMemory();
        FunctionManager fm = currentProgram.getFunctionManager();

        try (PrintWriter w = new PrintWriter(new FileWriter(outPath))) {
            w.println("Searching for LEA reg,[rip+disp32] instructions targeting " + targetAddr);
            for (MemoryBlock block : mem.getBlocks()) {
                if (!block.isExecute()) continue;
                long size = block.getSize();
                byte[] buf = new byte[(int) Math.min(size, Integer.MAX_VALUE - 16)];
                block.getBytes(block.getStart(), buf);
                int foundCount = 0;
                for (int i = 0; i + 7 <= buf.length; i++) {
                    // REX.W (0x48 or 0x4C for r8-r15 dest) + 0x8D (LEA) + modrm with mod=00, rm=101 (RIP-relative)
                    int rex = buf[i] & 0xFF;
                    if ((rex & 0xFE) != 0x48) continue; // 0x48 or 0x49/0x4C etc -- just check 0x40-0x4F REX range broadly
                    if ((rex & 0xF0) != 0x40) continue;
                    int opcode = buf[i + 1] & 0xFF;
                    if (opcode != 0x8D) continue;
                    int modrm = buf[i + 2] & 0xFF;
                    int mod = (modrm >> 6) & 3;
                    int rm = modrm & 7;
                    if (mod != 0 || rm != 5) continue; // RIP-relative addressing form only
                    int disp = (buf[i + 3] & 0xFF) | ((buf[i + 4] & 0xFF) << 8)
                            | ((buf[i + 5] & 0xFF) << 16) | ((buf[i + 6] & 0xFF) << 24);
                    Address insnAddr = block.getStart().add(i);
                    Address nextInsn = insnAddr.add(7);
                    Address computedTarget;
                    try {
                        computedTarget = nextInsn.add(disp);
                    } catch (Exception e) {
                        continue;
                    }
                    if (computedTarget.equals(targetAddr)) {
                        Function f = fm.getFunctionContaining(insnAddr);
                        w.println("MATCH: LEA at " + insnAddr + " -> " + computedTarget
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
