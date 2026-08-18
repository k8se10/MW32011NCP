// DumpDoublesAt.java -- reads a little-endian 8-byte double at each given hex address
// (x86 is little-endian; DumpFloatsAt.java's "big-endian 4-byte float" reading is wrong
// for an 8-byte double operand, e.g. an x87 `FMUL double ptr [addr]` -- confirmed via
// raw disassembly, not assumed, 2026-08-16 .menu rect-transform investigation).
// Usage: -postScript DumpDoublesAt.java <output_path> <addr1> [addr2 ...]
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.mem.Memory;
import java.io.FileWriter;
import java.io.PrintWriter;

public class DumpDoublesAt extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args == null || args.length < 2) { println("Usage: <output> <addr1> ..."); return; }
        String outPath = args[0];
        Memory mem = currentProgram.getMemory();
        try (PrintWriter w = new PrintWriter(new FileWriter(outPath))) {
            for (int i = 1; i < args.length; i++) {
                try {
                    Address a = currentProgram.getAddressFactory().getAddress(args[i]);
                    byte[] b = new byte[8];
                    mem.getBytes(a, b);
                    long bitsLE = 0;
                    for (int j = 7; j >= 0; j--) {
                        bitsLE = (bitsLE << 8) | (b[j] & 0xFFL);
                    }
                    double dLE = Double.longBitsToDouble(bitsLE);
                    StringBuilder hex = new StringBuilder();
                    for (byte bb : b) hex.append(String.format("%02X ", bb));
                    w.println(args[i] + " : bytes=" + hex.toString().trim() + " asDoubleLE=" + dLE);
                } catch (Exception e) {
                    w.println(args[i] + " : ERROR " + e.getMessage());
                }
            }
        }
        println("Wrote " + outPath);
    }
}
