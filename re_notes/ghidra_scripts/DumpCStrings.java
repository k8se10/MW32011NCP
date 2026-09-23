// DumpCStrings.java -- reads a null-terminated ASCII C string starting at each given address.
// Usage: -postScript DumpCStrings.java <output_path> <addr1> [addr2 ...]
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;

import java.io.FileWriter;
import java.io.PrintWriter;

public class DumpCStrings extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args == null || args.length < 2) {
            println("Usage: DumpCStrings.java <output_path> <addr1> [addr2 ...]");
            return;
        }
        String outPath = args[0];
        try (PrintWriter w = new PrintWriter(new FileWriter(outPath))) {
            for (int i = 1; i < args.length; i++) {
                Address addr = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(args[i]);
                StringBuilder sb = new StringBuilder();
                Address cur = addr;
                boolean printable = true;
                for (int n = 0; n < 128; n++) {
                    byte b = currentProgram.getMemory().getByte(cur);
                    if (b == 0) break;
                    if (b < 0x20 || b > 0x7e) { printable = false; break; }
                    sb.append((char) b);
                    cur = cur.add(1);
                }
                w.println(args[i] + " : " + (printable ? "\"" + sb.toString() + "\"" : "<non-printable, first bytes: " + Integer.toHexString(currentProgram.getMemory().getByte(addr) & 0xff) + " " + Integer.toHexString(currentProgram.getMemory().getByte(addr.add(1)) & 0xff) + " " + Integer.toHexString(currentProgram.getMemory().getByte(addr.add(2)) & 0xff) + " " + Integer.toHexString(currentProgram.getMemory().getByte(addr.add(3)) & 0xff) + ">"));
            }
        }
        println("Wrote report to " + outPath);
    }
}
