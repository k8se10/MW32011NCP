// DisassembleRange.java -- prints raw disassembly (mnemonic + bytes) for an address range.
// Usage: -postScript DisassembleRange.java <output_path> <startAddr> <lengthBytes>
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.mem.Memory;
import ghidra.program.disassemble.Disassembler;
import ghidra.util.task.TaskMonitor;

import java.io.FileWriter;
import java.io.PrintWriter;

public class DisassembleRange extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args == null || args.length < 3) {
            println("Usage: DisassembleRange.java <output_path> <startAddr> <lengthBytes>");
            return;
        }
        String outPath = args[0];
        Address start = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(args[1]);
        long length = Long.decode(args[2]);
        Address end = start.add(length);

        Listing listing = currentProgram.getListing();
        Disassembler disasm = Disassembler.getDisassembler(currentProgram, TaskMonitor.DUMMY, null);
        disasm.disassemble(start, null, true);

        try (PrintWriter w = new PrintWriter(new FileWriter(outPath))) {
            InstructionIterator it = listing.getInstructions(start, true);
            while (it.hasNext()) {
                Instruction insn = it.next();
                if (insn.getAddress().compareTo(end) >= 0) break;
                byte[] bytes = insn.getBytes();
                StringBuilder hex = new StringBuilder();
                for (byte b : bytes) hex.append(String.format("%02X ", b));
                w.println(insn.getAddress() + "  " + hex + " | " + insn.toString());
            }
        }
    }
}
