// CreateFuncAndDumpSig.java -- disassembles + creates a function at the given
// address (since this project runs Ghidra with -noanalysis for speed, no
// Function object exists there yet), then dumps its first N instructions'
// raw bytes for a runtime AOB signature, wildcarding any PC-relative operand.
// Usage: -postScript CreateFuncAndDumpSig.java <output_path> <funcAddr> [instructionCount]
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.symbol.Reference;
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.cmd.function.CreateFunctionCmd;

import java.io.FileWriter;
import java.io.PrintWriter;

public class CreateFuncAndDumpSig extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        String outPath = args[0];
        Address addr = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(args[1]);
        int count = args.length > 2 ? Integer.parseInt(args[2]) : 20;

        DisassembleCommand disCmd = new DisassembleCommand(addr, null, true);
        disCmd.applyTo(currentProgram, monitor);

        CreateFunctionCmd funcCmd = new CreateFunctionCmd(addr);
        funcCmd.applyTo(currentProgram, monitor);

        Function f = currentProgram.getFunctionManager().getFunctionAt(addr);
        Memory mem = currentProgram.getMemory();

        try (PrintWriter w = new PrintWriter(new FileWriter(outPath))) {
            if (f == null) {
                w.println("Failed to create function at " + addr);
                return;
            }
            w.println("Function created at " + addr + ", body size=" + f.getBody().getNumAddresses());
            InstructionIterator it = currentProgram.getListing().getInstructions(addr, true);
            int i = 0;
            StringBuilder rawHex = new StringBuilder();
            StringBuilder maskedHex = new StringBuilder();
            while (it.hasNext() && i < count) {
                Instruction insn = it.next();
                byte[] bytes = insn.getBytes();
                boolean hasRef = false;
                for (Reference ref : insn.getReferencesFrom()) {
                    if (ref.isMemoryReference()) { hasRef = true; break; }
                }
                StringBuilder hex = new StringBuilder();
                for (byte b : bytes) hex.append(String.format("%02x ", b));
                w.println(insn.getAddress() + ": " + hex.toString().trim() + "   ; " + insn.toString() + (hasRef ? "  [PC-REL/REF]" : ""));
                rawHex.append(hex);
                if (hasRef) {
                    // crude wildcard: mark last 4 bytes (typical rel32) as wildcard
                    int len = bytes.length;
                    for (int b = 0; b < len; b++) {
                        if (b >= len - 4) maskedHex.append("?? ");
                        else maskedHex.append(String.format("%02x ", bytes[b]));
                    }
                } else {
                    for (byte b : bytes) maskedHex.append(String.format("%02x ", b));
                }
                i++;
            }
            w.println();
            w.println("Raw:    " + rawHex.toString().trim());
            w.println("Masked: " + maskedHex.toString().trim());
        }
        println("Wrote " + outPath);
    }
}
