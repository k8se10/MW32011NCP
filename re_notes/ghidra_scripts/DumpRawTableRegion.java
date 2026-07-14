// DumpRawTableRegion.java — dumps raw dwords around given addresses to help infer an
// unknown table's entry structure/stride, and for each dword that looks like a valid
// pointer, reports whether it resolves to a known function entry point (and its name).
//
// Usage: -postScript DumpRawTableRegion.java <output_path> <addr1> [<addr2> ...]
// (dumps 6 dwords before and 10 dwords after each given address)

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.mem.Memory;

import java.io.FileWriter;
import java.io.PrintWriter;

public class DumpRawTableRegion extends GhidraScript {

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args == null || args.length < 2) {
            println("Usage: DumpRawTableRegion.java <output_path> <addr1> ...");
            return;
        }
        String outPath = args[0];
        Memory mem = currentProgram.getMemory();

        try (PrintWriter w = new PrintWriter(new FileWriter(outPath))) {
            for (int i = 1; i < args.length; i++) {
                Address center = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(args[i]);
                w.println("=== around " + center + " ===");
                for (int off = -24; off <= 40; off += 4) {
                    Address a = center.add(off);
                    try {
                        int val = mem.getInt(a);
                        String note = "";
                        Address asPtr = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(val & 0xFFFFFFFFL);
                        Function f = currentProgram.getFunctionManager().getFunctionAt(asPtr);
                        if (f != null) {
                            note = "  <-- FUNCTION: " + f.getName();
                        } else if (mem.contains(asPtr)) {
                            try {
                                byte b0 = mem.getByte(asPtr);
                                if (b0 >= 32 && b0 <= 126) {
                                    StringBuilder sb = new StringBuilder();
                                    for (int k = 0; k < 40; k++) {
                                        byte b = mem.getByte(asPtr.add(k));
                                        if (b == 0) break;
                                        if (b < 32 || b > 126) { sb = null; break; }
                                        sb.append((char) b);
                                    }
                                    if (sb != null) note = "  <-- maybe string: \"" + sb + "\"";
                                }
                            } catch (Exception ignored) {}
                        }
                        w.println(String.format("  [%s%+d] = 0x%08x%s", center, off, val, note));
                    } catch (Exception e) {
                        w.println(String.format("  [%s%+d] = <unreadable>", center, off));
                    }
                }
                w.println();
            }
        }
        println("Wrote report to " + outPath);
    }
}
