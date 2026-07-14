// DumpKbState.java — for a list of kbutton table entry addresses, reads the second
// dword (pointer to that bind's individually-declared kbutton_t state struct), dumps
// its first 32 raw bytes, and lists every function in the binary that references that
// struct address (read or write) -- this is how we find which usercmd-building function
// actually consumes a given bind's held/pressed state.
//
// Usage: -postScript DumpKbState.java <output_path> <entryAddr1> [entryAddr2 ...]

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.symbol.ReferenceManager;

import java.io.FileWriter;
import java.io.PrintWriter;

public class DumpKbState extends GhidraScript {

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args == null || args.length < 2) {
            println("Usage: DumpKbState.java <output_path> <entryAddr1> [entryAddr2 ...]");
            return;
        }
        String outPath = args[0];
        Memory mem = currentProgram.getMemory();
        FunctionManager fm = currentProgram.getFunctionManager();
        ReferenceManager refMgr = currentProgram.getReferenceManager();

        try (PrintWriter w = new PrintWriter(new FileWriter(outPath))) {
            for (int i = 1; i < args.length; i++) {
                Address entryAddr = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(args[i]);
                w.println("=== Entry @ " + entryAddr + " ===");
                try {
                    int namePtrRaw = mem.getInt(entryAddr);
                    Address nameAddr = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(namePtrRaw & 0xFFFFFFFFL);
                    StringBuilder name = new StringBuilder();
                    for (int k = 0; k < 32; k++) {
                        byte b = mem.getByte(nameAddr.add(k));
                        if (b == 0) break;
                        name.append((char) b);
                    }
                    w.println("name = " + name);

                    int stateRaw = mem.getInt(entryAddr.add(4));
                    Address stateAddr = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(stateRaw & 0xFFFFFFFFL);
                    w.println("kbutton_t* = " + stateAddr);

                    StringBuilder hex = new StringBuilder();
                    for (int k = 0; k < 32; k++) {
                        byte b = mem.getByte(stateAddr.add(k));
                        hex.append(String.format("%02x ", b));
                    }
                    w.println("raw bytes = " + hex);

                    w.println("--- references TO " + stateAddr + " ---");
                    ReferenceIterator refs = refMgr.getReferencesTo(stateAddr);
                    int count = 0;
                    while (refs.hasNext()) {
                        Reference ref = refs.next();
                        count++;
                        Address from = ref.getFromAddress();
                        Function func = fm.getFunctionContaining(from);
                        w.println("  from " + from + (func != null ? " IN FUNCTION " + func.getName() + "@" + func.getEntryPoint() : " IN DATA"));
                    }
                    w.println("  (" + count + " references)");
                } catch (Exception e) {
                    w.println("  ERROR: " + e.getMessage());
                }
                w.println();
            }
        }
        println("Wrote report to " + outPath);
    }
}
