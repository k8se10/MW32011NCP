// DumpFloatsAt.java -- reads a big-endian 4-byte float at each given hex address.
// Usage: -postScript DumpFloatsAt.java <output_path> <addr1> [addr2 ...]
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.mem.Memory;
import java.io.FileWriter;
import java.io.PrintWriter;

public class DumpFloatsAt extends GhidraScript {
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
                    byte[] b = new byte[4];
                    mem.getBytes(a, b);
                    int bits = ((b[0]&0xFF)<<24)|((b[1]&0xFF)<<16)|((b[2]&0xFF)<<8)|(b[3]&0xFF);
                    float f = Float.intBitsToFloat(bits);
                    int asIntBE = bits;
                    w.println(args[i] + " : bytes=" + String.format("%02X %02X %02X %02X", b[0],b[1],b[2],b[3])
                        + " asFloat=" + f + " asInt32=" + asIntBE);
                } catch (Exception e) {
                    w.println(args[i] + " : ERROR " + e.getMessage());
                }
            }
        }
        println("Wrote " + outPath);
    }
}
