// DumpTableXrefs.java -- dumps raw dwords in a range and reports xrefs (from code)
// to each dword-aligned address in that range, to find a jump/dispatch table.
// Usage: -postScript DumpTableXrefs.java <output_path> <startAddr> <endAddr>
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.symbol.ReferenceManager;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import java.io.FileWriter;
import java.io.PrintWriter;

public class DumpTableXrefs extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        String outPath = args[0];
        Address start = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(args[1]);
        Address end = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(args[2]);
        Memory mem = currentProgram.getMemory();
        ReferenceManager rm = currentProgram.getReferenceManager();
        FunctionManager fm = currentProgram.getFunctionManager();
        try (PrintWriter w = new PrintWriter(new FileWriter(outPath))) {
            Address a = start;
            while (a.compareTo(end) <= 0) {
                long val = mem.getInt(a) & 0xFFFFFFFFL;
                w.println(a + " : 0x" + Long.toHexString(val));
                ReferenceIterator refs = rm.getReferencesTo(a);
                while (refs.hasNext()) {
                    Reference r = refs.next();
                    Function f = fm.getFunctionContaining(r.getFromAddress());
                    w.println("    xref-to-this-slot from " + r.getFromAddress() + (f != null ? " in " + f.getName() : "") + " type=" + r.getReferenceType());
                }
                a = a.add(4);
            }
        }
        println("done");
    }
}
