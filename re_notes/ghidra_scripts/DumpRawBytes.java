// DumpRawBytes.java — dumps raw bytes at given addresses (as unsigned decimal + hex),
// for reading a small byte-granular lookup table entry directly rather than guessing
// at its meaning from surrounding code.
//
// Usage: -postScript DumpRawBytes.java <output_path> <addr1> [<count1>] <addr2> [<count2>] ...
// If a count is omitted for an address, defaults to 8 bytes from that address.
// To pass an explicit count, put it as the NEXT arg immediately after an address that
// starts with "n:" is not supported -- simplest form: one address per invocation, or
// call repeatedly. This script reads args pairwise as (addr, count) when count looks
// like a small plain integer (<=64), else treats each arg as its own address with the
// default count.

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.mem.Memory;

import java.io.FileWriter;
import java.io.PrintWriter;

public class DumpRawBytes extends GhidraScript {

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args == null || args.length < 2) {
            println("Usage: DumpRawBytes.java <output_path> <addr1> [count1] ...");
            return;
        }
        String outPath = args[0];
        Memory mem = currentProgram.getMemory();

        try (PrintWriter w = new PrintWriter(new FileWriter(outPath))) {
            int i = 1;
            while (i < args.length) {
                String addrStr = args[i];
                int count = 8;
                if (i + 1 < args.length) {
                    try {
                        int maybeCount = Integer.parseInt(args[i + 1]);
                        if (maybeCount > 0 && maybeCount <= 64) {
                            count = maybeCount;
                            i++;
                        }
                    } catch (NumberFormatException e) {
                        // next arg isn't a count, treat as a new address
                    }
                }
                Address addr = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(addrStr);
                w.println("=== " + addr + " (" + count + " bytes) ===");
                for (int k = 0; k < count; k++) {
                    Address a = addr.add(k);
                    byte b = mem.getByte(a);
                    int u = b & 0xff;
                    w.println("  " + a + " : " + u + " (0x" + Integer.toHexString(u) + ")");
                }
                w.println();
                i++;
            }
        }
        println("Wrote report to " + outPath);
    }
}
