// DumpKbHandlers.java — for a list of kbutton table entry addresses, reads the second
// dword (handler function pointer) and dumps that handler function's full disassembly.
// Table entry layout confirmed as {char* name; void* handlerFuncPtr;} (NOT a shared
// kbutton_t state array with fixed stride -- DumpEntryPtrs.java showed the 5 movement
// entries point to 5 completely different, non-strided addresses, i.e. distinct funcs).
//
// Usage: -postScript DumpKbHandlers.java <output_path> <entryAddr1> [entryAddr2 ...]

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.mem.Memory;

import java.io.FileWriter;
import java.io.PrintWriter;

public class DumpKbHandlers extends GhidraScript {

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args == null || args.length < 2) {
            println("Usage: DumpKbHandlers.java <output_path> <entryAddr1> [entryAddr2 ...]");
            return;
        }
        String outPath = args[0];
        Memory mem = currentProgram.getMemory();
        Listing listing = currentProgram.getListing();

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

                    int handlerRaw = mem.getInt(entryAddr.add(4));
                    Address handlerAddr = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(handlerRaw & 0xFFFFFFFFL);
                    w.println("handlerPtr = " + handlerAddr);

                    Function func = currentProgram.getFunctionManager().getFunctionAt(handlerAddr);
                    if (func == null) {
                        w.println("  (no function defined at handlerPtr -- raw bytes follow, may not be code)");
                        w.println();
                        continue;
                    }
                    w.println("  function = " + func.getName() + " size=" + func.getBody().getNumAddresses());
                    InstructionIterator it = listing.getInstructions(func.getBody(), true);
                    while (it.hasNext()) {
                        Instruction insn = it.next();
                        w.println("    " + insn.getAddress() + "  " + insn.toString());
                    }
                } catch (Exception e) {
                    w.println("  ERROR: " + e.getMessage());
                }
                w.println();
            }
        }
        println("Wrote report to " + outPath);
    }
}
