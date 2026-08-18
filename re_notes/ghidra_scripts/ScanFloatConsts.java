// ScanFloatConsts.java -- brute-force scans all initialized memory blocks for 4-byte
// little-endian float bit patterns matching any of the given target values (e.g. 640.0,
// 480.0 -- classic idTech3 UI virtual-resolution candidates). Reports every address
// whose 4 bytes decode to a target value, so candidates can be cross-checked with
// DescribeRefs.java for real code references (a raw byte match alone proves nothing --
// could be inside unrelated data/padding).
// Usage: -postScript ScanFloatConsts.java <output_path> <targetFloat1> [targetFloat2 ...]
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSetView;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;
import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.List;

public class ScanFloatConsts extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args == null || args.length < 2) { println("Usage: <output> <targetFloat1> ..."); return; }
        String outPath = args[0];
        List<Float> targets = new ArrayList<>();
        for (int i = 1; i < args.length; i++) targets.add(Float.parseFloat(args[i]));

        Memory mem = currentProgram.getMemory();
        try (PrintWriter w = new PrintWriter(new FileWriter(outPath))) {
            for (MemoryBlock block : mem.getBlocks()) {
                if (!block.isInitialized() || !block.isLoaded()) continue;
                Address start = block.getStart();
                long size = block.getSize();
                byte[] buf = new byte[(int) Math.min(size, Integer.MAX_VALUE - 8)];
                try {
                    mem.getBytes(start, buf);
                } catch (Exception e) { continue; }
                for (int off = 0; off + 4 <= buf.length; off++) {
                    int bits = (buf[off] & 0xFF) | ((buf[off+1] & 0xFF) << 8)
                             | ((buf[off+2] & 0xFF) << 16) | ((buf[off+3] & 0xFF) << 24);
                    float f = Float.intBitsToFloat(bits);
                    for (Float t : targets) {
                        if (f == t) {
                            Address a = start.add(off);
                            w.println(a + " (block " + block.getName() + ") = " + f);
                        }
                    }
                }
            }
        }
        println("Wrote " + outPath);
    }
}
