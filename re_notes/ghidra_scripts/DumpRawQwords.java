// DumpRawQwords.java — x64 equivalent of DumpRawDwords.java. Dumps every 8-byte qword
// in [startAddr, endAddr) along with what Ghidra thinks is at the address that qword
// points to (function name, string value, or nothing known) — used to spot table
// layouts (e.g. {namePtr, negNamePtr, kbuttonPtr} triples) directly from static
// initialized data on x64 binaries, without needing runtime memory.
//
// Usage: -postScript DumpRawQwords.java <output_path> <startAddr> <endAddr>

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.Function;
import ghidra.program.model.mem.Memory;

import java.io.FileWriter;
import java.io.PrintWriter;

public class DumpRawQwords extends GhidraScript {

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args == null || args.length < 3) {
            println("Usage: DumpRawQwords.java <output_path> <startAddr> <endAddr>");
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
                    val = mem.getLong(addr);
                } catch (Exception e) {
                    w.println(addr + " : <unreadable>");
                    addr = addr.add(8);
                    continue;
                }
                String desc = "";
                try {
                    Address target = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(val);
                    Function f = currentProgram.getFunctionManager().getFunctionAt(target);
                    if (f != null) {
                        desc = "-> FUNCTION " + f.getName();
                    } else {
                        Data d = currentProgram.getListing().getDataAt(target);
                        if (d != null && d.hasStringValue()) {
                            desc = "-> STRING \"" + d.getValue() + "\"";
                        }
                    }
                } catch (Exception e) {
                    // val isn't a valid address in this space; leave desc blank
                }
                w.println(addr + " : 0x" + Long.toHexString(val) + "  " + desc);
                addr = addr.add(8);
            }
        }
        println("Wrote report to " + outPath);
    }
}
