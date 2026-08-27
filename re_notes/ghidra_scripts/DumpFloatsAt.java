// DumpFloatsAt.java -- reads the raw 4 bytes at each given address and prints it
// interpreted as both a little-endian int32 and an IEEE754 float.
//
// Usage: -postScript DumpFloatsAt.java <output_path> <addr1> [addr2] ...

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.mem.Memory;

import java.io.FileWriter;
import java.io.PrintWriter;

public class DumpFloatsAt extends GhidraScript {

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args == null || args.length < 2) {
            println("Usage: DumpFloatsAt.java <output_path> <addr1> [addr2] ...");
            return;
        }
        String outPath = args[0];
        Memory mem = currentProgram.getMemory();

        try (PrintWriter w = new PrintWriter(new FileWriter(outPath))) {
            for (int i = 1; i < args.length; i++) {
                String hex = args[i];
                Address addr = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(hex);
                byte[] b = new byte[4];
                try {
                    mem.getBytes(addr, b);
                } catch (Exception e) {
                    w.println(addr + "\tERROR " + e.getMessage());
                    continue;
                }
                int bits = (b[0] & 0xFF) | ((b[1] & 0xFF) << 8) | ((b[2] & 0xFF) << 16) | ((b[3] & 0xFF) << 24);
                float f = Float.intBitsToFloat(bits);
                w.println(addr + "\tint32=" + bits + "\thex=0x" + Integer.toHexString(bits) + "\tfloat=" + f);
            }
        }
        println("Wrote float dump to " + outPath);
    }
}
