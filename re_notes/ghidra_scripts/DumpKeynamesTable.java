// DumpKeynamesTable.java -- walks a null-terminated {const char* name; int keynum;}
// table (the classic idTech/Quake3 keynames_t pattern) starting at a given address,
// printing (keynum, name) pairs until a null name pointer is hit.
//
// Usage: -postScript DumpKeynamesTable.java <output_path> <tableAddr> [maxEntries]

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.mem.Memory;

import java.io.FileWriter;
import java.io.PrintWriter;

public class DumpKeynamesTable extends GhidraScript {

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args == null || args.length < 2) {
            println("Usage: DumpKeynamesTable.java <output_path> <tableAddr> [maxEntries]");
            return;
        }
        String outPath = args[0];
        Address tableAddr = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(args[1]);
        int maxEntries = args.length > 2 ? Integer.parseInt(args[2]) : 300;
        Memory mem = currentProgram.getMemory();

        try (PrintWriter w = new PrintWriter(new FileWriter(outPath))) {
            Address entry = tableAddr;
            for (int i = 0; i < maxEntries; i++) {
                int namePtrRaw = mem.getInt(entry);
                int keynum = mem.getInt(entry.add(4));
                if (namePtrRaw == 0) {
                    w.println("-- end of table (null name) after " + i + " entries --");
                    break;
                }
                Address namePtr = currentProgram.getAddressFactory().getDefaultAddressSpace()
                        .getAddress(namePtrRaw & 0xFFFFFFFFL);
                StringBuilder sb = new StringBuilder();
                try {
                    for (int k = 0; k < 64; k++) {
                        byte b = mem.getByte(namePtr.add(k));
                        if (b == 0) break;
                        sb.append((char) (b & 0xFF));
                    }
                } catch (Exception e) {
                    sb.append("<unreadable @ ").append(namePtr).append(">");
                }
                w.println(String.format("keynum=0x%02x (%d)  name=\"%s\"", keynum, keynum, sb.toString()));
                entry = entry.add(8);
            }
        }
        println("Wrote report to " + outPath);
    }
}
