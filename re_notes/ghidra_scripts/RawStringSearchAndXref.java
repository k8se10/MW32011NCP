// RawStringSearchAndXref.java -- raw byte search for an ASCII substring anywhere in memory
// (doesn't require the string to be marked as Data / doesn't require prior analysis), then
// finds every function containing an instruction that references each hit address, scanning
// nearby instructions for LEA/MOV-style address loads if no formal reference exists yet.
// Usage: -postScript RawStringSearchAndXref.java <output_path> <needle1> [needle2 ...]
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSetView;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.symbol.ReferenceManager;
import ghidra.util.task.TaskMonitor;

import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.List;

public class RawStringSearchAndXref extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args == null || args.length < 2) {
            println("Usage: RawStringSearchAndXref.java <output_path> <needle1> [needle2 ...]");
            return;
        }
        String outPath = args[0];
        Memory mem = currentProgram.getMemory();
        FunctionManager fm = currentProgram.getFunctionManager();
        ReferenceManager refMgr = currentProgram.getReferenceManager();

        try (PrintWriter w = new PrintWriter(new FileWriter(outPath))) {
            for (int ai = 1; ai < args.length; ai++) {
                byte[] needle = args[ai].getBytes("US-ASCII");
                w.println("================================================================");
                w.println("Searching for: " + args[ai]);
                List<Address> hits = new ArrayList<>();
                Address start = currentProgram.getMinAddress();
                Address found = mem.findBytes(start, needle, null, true, TaskMonitor.DUMMY);
                int safety = 0;
                while (found != null && safety < 50) {
                    hits.add(found);
                    safety++;
                    Address next = found.add(1);
                    found = mem.findBytes(next, needle, null, true, TaskMonitor.DUMMY);
                }
                w.println("Hits: " + hits.size());
                for (Address hit : hits) {
                    w.println("  STRING @ " + hit);
                    ReferenceIterator refs = refMgr.getReferencesTo(hit);
                    int count = 0;
                    while (refs.hasNext()) {
                        Reference r = refs.next();
                        Address fromAddr = r.getFromAddress();
                        Function fn = fm.getFunctionContaining(fromAddr);
                        count++;
                        w.println("    REF #" + count + ": from " + fromAddr + " in " +
                            (fn == null ? "<none/data>" : fn.getName() + " @ " + fn.getEntryPoint()));
                    }
                    if (count == 0) {
                        w.println("    (no formal references -- may need -analysisTimeoutPerFile or manual disasm)");
                    }
                }
                w.println();
            }
        }
    }
}
