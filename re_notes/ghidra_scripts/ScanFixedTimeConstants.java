// ScanFixedTimeConstants.java -- systematic scan for a hardcoded ~30Hz/33ms
// simulation-tick constant, independent of the already-investigated `fixedtime`
// dvar path (known_issues.md issue #87/#79/60fps-tick-feature, 2026-08-25 passes).
//
// Walks every instruction in the executable code, and every defined/undefined
// DWORD-aligned dword in initialized data, looking for immediate operands or
// raw 4-byte values matching:
//   - int 30, 33, 34 (common tick-interval constants in ms or Hz)
//   - float 0.033333f / 0.0333333f (1/30 sec, IEEE754 0x3D888889 +/- 1 ULP band)
//   - float 0.0166667f (1/60 sec, for comparison/contrast)
//   - float 33.333f / 33.0f (ms form of the same constant)
//
// This is the "blunt float-bit-pattern scan across the whole binary" explicitly
// flagged as the right next tool and NOT YET RUN in known_issues.md's 60fps-tick
// investigation (2026-08-25, "Still genuinely unresolved" section).
//
// Usage: -postScript ScanFixedTimeConstants.java <output_path>

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSetView;
import ghidra.program.model.listing.CodeUnit;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.scalar.Scalar;

import java.io.FileWriter;
import java.io.PrintWriter;

public class ScanFixedTimeConstants extends GhidraScript {

    private static boolean nearFloat(float v, float target, float tol) {
        return Math.abs(v - target) <= tol;
    }

    private String classifyInt(long v) {
        if (v == 30) return "int30";
        if (v == 33) return "int33";
        if (v == 34) return "int34";
        return null;
    }

    private String classifyFloat(int bits) {
        float f = Float.intBitsToFloat(bits);
        if (Float.isNaN(f) || Float.isInfinite(f)) return null;
        if (nearFloat(f, 0.033333f, 0.00002f)) return "f_1over30sec";
        if (nearFloat(f, 0.0166667f, 0.00002f)) return "f_1over60sec";
        if (nearFloat(f, 33.333f, 0.01f)) return "f_33_333ms";
        if (nearFloat(f, 33.0f, 0.0001f)) return "f_33_0";
        if (nearFloat(f, 30.0f, 0.0001f)) return "f_30_0";
        return null;
    }

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args == null || args.length < 1) {
            println("Usage: ScanFixedTimeConstants.java <output_path>");
            return;
        }
        String outPath = args[0];

        Listing listing = currentProgram.getListing();
        Memory mem = currentProgram.getMemory();

        int instrHits = 0;
        int dataHits = 0;

        try (PrintWriter w = new PrintWriter(new FileWriter(outPath))) {
            w.println("=== Instruction immediate-operand scan ===");
            InstructionIterator it = listing.getInstructions(true);
            while (it.hasNext() && !monitor.isCancelled()) {
                Instruction instr = it.next();
                int numOps = instr.getNumOperands();
                for (int i = 0; i < numOps; i++) {
                    Object[] objs = instr.getOpObjects(i);
                    for (Object o : objs) {
                        if (!(o instanceof Scalar)) continue;
                        Scalar s = (Scalar) o;
                        long v = s.getSignedValue();
                        String intTag = classifyInt(v);
                        if (intTag != null) {
                            w.println(instr.getAddress() + "\t" + intTag + "\tv=" + v
                                    + "\t" + instr);
                            instrHits++;
                        }
                        if (s.bitLength() <= 32) {
                            int bits = (int) s.getUnsignedValue();
                            String fTag = classifyFloat(bits);
                            if (fTag != null) {
                                w.println(instr.getAddress() + "\t" + fTag + "\tbits=0x"
                                        + Integer.toHexString(bits) + "\t" + instr);
                                instrHits++;
                            }
                        }
                    }
                }
                if ((instrHits + dataHits) % 5000 == 0) monitor.checkCancelled();
            }

            w.println("=== Raw initialized-data dword scan ===");
            for (MemoryBlock block : mem.getBlocks()) {
                if (!block.isInitialized() || !block.isRead()) continue;
                long size = block.getSize();
                if (size > 64L * 1024 * 1024) continue;
                byte[] data = new byte[(int) size];
                try {
                    block.getBytes(block.getStart(), data);
                } catch (Exception e) {
                    continue;
                }
                for (int off = 0; off + 4 <= data.length; off += 4) {
                    int bits = (data[off] & 0xFF) | ((data[off + 1] & 0xFF) << 8)
                            | ((data[off + 2] & 0xFF) << 16) | ((data[off + 3] & 0xFF) << 24);
                    String fTag = classifyFloat(bits);
                    if (fTag != null) {
                        Address a = block.getStart().add(off);
                        w.println(a + "\tDATA_" + fTag + "\tbits=0x" + Integer.toHexString(bits));
                        dataHits++;
                    }
                }
            }

            w.println("DONE instrHits=" + instrHits + " dataHits=" + dataHits);
        }
        println("Wrote scan results to " + outPath + " (instrHits=" + instrHits
                + " dataHits=" + dataHits + ")");
    }
}
