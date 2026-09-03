// DumpSigBytes.java -- dumps raw instruction bytes (with per-instruction boundaries
// and Ghidra's own operand analysis) for the first N instructions at a function's
// entry point, specifically to build a real AOB (Array-of-Bytes) runtime signature:
// each instruction's raw bytes, its mnemonic/operand text, and (crucially) whether it
// has a PC-relative reference (a CALL/JMP rel32, or a RIP-relative LEA/MOV displacement)
// that would need to be wildcarded out of the signature, since that byte range encodes
// an address that WILL shift between binary builds even if the surrounding code is
// byte-identical.
//
// Usage: -postScript DumpSigBytes.java <output_path> <funcAddr> [instructionCount]
// instructionCount defaults to 20.

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.symbol.Reference;

import java.io.FileWriter;
import java.io.PrintWriter;

public class DumpSigBytes extends GhidraScript {

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args == null || args.length < 2) {
            println("Usage: DumpSigBytes.java <output_path> <funcAddr> [instructionCount]");
            return;
        }
        String outPath = args[0];
        Address funcAddr = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(args[1]);
        int maxInsns = args.length >= 3 ? Integer.parseInt(args[2]) : 20;

        Function func = currentProgram.getFunctionManager().getFunctionAt(funcAddr);
        if (func == null) {
            println("No function at " + args[1]);
            return;
        }

        Memory mem = currentProgram.getMemory();
        Listing listing = currentProgram.getListing();
        InstructionIterator it = listing.getInstructions(func.getBody(), true);

        try (PrintWriter w = new PrintWriter(new FileWriter(outPath))) {
            w.println("Signature-byte dump of " + func.getName() + " @ " + func.getEntryPoint());
            w.println("(byte offsets relative to function entry point)");
            w.println();
            int count = 0;
            int totalLen = 0;
            StringBuilder rawHex = new StringBuilder();
            StringBuilder maskedHex = new StringBuilder();
            while (it.hasNext() && count < maxInsns) {
                Instruction insn = it.next();
                int len = insn.getLength();
                byte[] bytes = new byte[len];
                mem.getBytes(insn.getAddress(), bytes);

                boolean hasPcRelative = false;
                Reference[] refs = insn.getReferencesFrom();
                for (Reference ref : refs) {
                    if (ref.getReferenceType().isFlow() || ref.getReferenceType().isData()) {
                        // Any CALL/JMP rel32 or RIP-relative LEA/MOV displacement embeds an
                        // address/offset in its own bytes -- flag it so a human reviews
                        // whether it needs wildcarding in the final signature.
                        hasPcRelative = true;
                    }
                }

                StringBuilder byteStr = new StringBuilder();
                for (byte b : bytes) {
                    byteStr.append(String.format("%02X ", b & 0xff));
                    rawHex.append(String.format("%02X ", b & 0xff));
                    maskedHex.append(hasPcRelative ? "?? " : String.format("%02X ", b & 0xff));
                }

                w.println(String.format("+0x%02X  %-30s %-40s %s",
                        totalLen, byteStr.toString().trim(), insn.toString(),
                        hasPcRelative ? "[PC-RELATIVE/REF -- likely needs wildcarding]" : ""));

                totalLen += len;
                count++;
            }
            w.println();
            w.println("Raw bytes (" + totalLen + " total): " + rawHex.toString().trim());
            w.println();
            w.println("Suggested masked signature (PC-relative/ref instructions fully");
            w.println("wildcarded -- refine by hand, this is a starting point only):");
            w.println(maskedHex.toString().trim());
        }
        println("Wrote report to " + outPath);
    }
}
