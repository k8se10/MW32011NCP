// DumpRawDwords.java — dumps every 4-byte dword in [startAddr, endAddr) along with what
// Ghidra thinks is at the address that dword points to (function name, string value, or
// nothing known) -- used to spot table layouts (e.g. {namePtr, callbackPtr} pairs)
// directly from static initialized data, without needing runtime memory.
//
// Usage: -postScript DumpRawDwords.java <output_path> <startAddr> <endAddr>

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.Function;
import ghidra.program.model.mem.Memory;

import java.io.FileWriter;
import java.io.PrintWriter;

public class DumpRawDwords extends GhidraScript {

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args == null || args.length < 3) {
            println("Usage: DumpRawDwords.java <output_path> <startAddr> <endAddr>");
            return;
        }
        String outPath = args[0];
        Address start = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(args[1]);
        Address end = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(args[2]);
        Memory mem = currentProgram.getMemory();

        try (PrintWriter w = new PrintWriter(new FileWriter(outPath))) {
            Address addr = start;
            while (addr.compareTo(end) < 0) {
                long val;
                try {
                    val = mem.getInt(addr) & 0xFFFFFFFFL;
                } catch (Exception e) {
                    w.println(addr + " : <unreadable>");
                    addr = addr.add(4);
                    continue;
                }
                Address target = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(val);
                String desc = "";
                Function f = currentProgram.getFunctionManager().getFunctionAt(target);
                if (f != null) {
                    desc = "-> FUNCTION " + f.getName();
                } else {
                    Data d = currentProgram.getListing().getDataAt(target);
                    if (d != null && d.hasStringValue()) {
                        desc = "-> STRING \"" + d.getValue() + "\"";
                    }
                }
                w.println(addr + " : 0x" + Long.toHexString(val) + "  " + desc);
                addr = addr.add(4);
            }
        }
        println("Wrote report to " + outPath);
    }
}
