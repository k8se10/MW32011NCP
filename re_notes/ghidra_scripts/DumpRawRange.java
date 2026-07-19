// DumpRawRange.java -- disassembles a raw address range regardless of existing function
// boundaries, creating instructions as needed. Used to find a function's TRUE entry
// point by looking backward from a known internal address for the previous function's
// RET / this function's own prologue.
//
// Usage: -postScript DumpRawRange.java <output_path> <startAddr> <endAddr>

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.Listing;
import ghidra.program.disassemble.Disassembler;

import java.io.FileWriter;
import java.io.PrintWriter;

public class DumpRawRange extends GhidraScript {

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args == null || args.length < 3) {
            println("Usage: DumpRawRange.java <output_path> <startAddr> <endAddr>");
            return;
        }
        String outPath = args[0];
        Address start = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(args[1]);
        Address end = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(args[2]);

        Disassembler disasm = Disassembler.getDisassembler(currentProgram, monitor, null);
        AddressSet set = new AddressSet(start, end);
        disasm.disassemble(start, set, true);

        Listing listing = currentProgram.getListing();
        try (PrintWriter w = new PrintWriter(new FileWriter(outPath))) {
            Address cur = start;
            while (cur.compareTo(end) <= 0) {
                Instruction insn = listing.getInstructionAt(cur);
                if (insn != null) {
                    w.println(insn.getAddress() + "  " + insn.toString());
                    cur = insn.getMaxAddress().add(1);
                } else {
                    byte b = currentProgram.getMemory().getByte(cur);
                    w.println(cur + "  db 0x" + Integer.toHexString(b & 0xff));
                    cur = cur.add(1);
                }
            }
        }
        println("Wrote report to " + outPath);
    }
}
