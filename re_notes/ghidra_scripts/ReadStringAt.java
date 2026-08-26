// ReadStringAt.java -- reads a raw null-terminated ASCII string starting at a given
// address, regardless of whether Ghidra has it defined as a string data item. Used for
// data references the decompiler shows as "&DAT_xxxxxxxx" (unrecognized as a string)
// instead of a proper string literal.
//
// Usage: -postScript ReadStringAt.java <output_path> <addr1> [addr2 ...]

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.mem.Memory;

import java.io.FileWriter;
import java.io.PrintWriter;

public class ReadStringAt extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args == null || args.length < 2) {
            println("Usage: ReadStringAt.java <output_path> <addr1> [addr2 ...]");
            return;
        }
        String outPath = args[0];
        Memory mem = currentProgram.getMemory();

        try (PrintWriter w = new PrintWriter(new FileWriter(outPath))) {
            for (int i = 1; i < args.length; i++) {
                Address addr = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(args[i]);
                StringBuilder sb = new StringBuilder();
                Address cur = addr;
                for (int n = 0; n < 512; n++) {
                    byte b;
                    try {
                        b = mem.getByte(cur);
                    } catch (Exception e) {
                        sb.append("<read error at +").append(n).append(">");
                        break;
                    }
                    if (b == 0) break;
                    if (b >= 0x20 && b < 0x7f) {
                        sb.append((char) b);
                    } else {
                        sb.append("\\x").append(String.format("%02x", b));
                    }
                    cur = cur.add(1);
                }
                w.println(addr + " = \"" + sb.toString() + "\"");
            }
        }
        println("Wrote report to " + outPath);
    }
}
