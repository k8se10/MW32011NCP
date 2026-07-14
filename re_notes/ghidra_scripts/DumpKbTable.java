// DumpKbTable.java — Ghidra headless GhidraScript.
// Walks outward (both directions) from a known table-entry address in 8-byte steps,
// treating each entry as {char* name; void* kbButtonPtr;}. Stops each direction once
// the first dword no longer looks like a pointer to a short, printable, null-terminated
// string starting with '+' or '-' (bind command convention). Prints every valid entry
// found, plus references TO the inferred table base (to find the processing loop).
//
// Usage: -postScript DumpKbTable.java <output_path> <knownEntryAddr>

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.symbol.ReferenceManager;

import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.List;

public class DumpKbTable extends GhidraScript {

    Memory mem;

    String readCString(Address addr, int maxLen) {
        try {
            StringBuilder sb = new StringBuilder();
            for (int i = 0; i < maxLen; i++) {
                byte b = mem.getByte(addr.add(i));
                if (b == 0) return sb.toString();
                if (b < 32 || b > 126) return null; // not printable ascii
                sb.append((char) b);
            }
            return null; // too long / no terminator
        } catch (Exception e) {
            return null;
        }
    }

    // Returns the name string if this looks like a valid {name*, kb*} entry, else null.
    String tryReadEntry(Address entryAddr) {
        try {
            int namePtr = mem.getInt(entryAddr);
            if (namePtr == 0) return null;
            Address nameAddr = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(namePtr & 0xFFFFFFFFL);
            String s = readCString(nameAddr, 32);
            if (s == null || s.isEmpty()) return null;
            if (s.charAt(0) != '+' && s.charAt(0) != '-') return null;
            return s;
        } catch (Exception e) {
            return null;
        }
    }

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args == null || args.length < 2) {
            println("Usage: DumpKbTable.java <output_path> <knownEntryAddr>");
            return;
        }
        String outPath = args[0];
        mem = currentProgram.getMemory();

        Address known = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(args[1]);
        int stride = 8;

        List<String> entries = new ArrayList<>();
        List<Address> entryAddrs = new ArrayList<>();

        // scan backward
        List<String> backward = new ArrayList<>();
        List<Address> backwardAddrs = new ArrayList<>();
        Address cursor = known.subtract(stride);
        while (true) {
            String s = tryReadEntry(cursor);
            if (s == null) break;
            backward.add(s);
            backwardAddrs.add(cursor);
            cursor = cursor.subtract(stride);
        }
        for (int i = backward.size() - 1; i >= 0; i--) {
            entries.add(backward.get(i));
            entryAddrs.add(backwardAddrs.get(i));
        }

        // known entry itself
        String knownName = tryReadEntry(known);
        entries.add(knownName != null ? knownName : "(?)");
        entryAddrs.add(known);

        // scan forward
        cursor = known.add(stride);
        while (true) {
            String s = tryReadEntry(cursor);
            if (s == null) break;
            entries.add(s);
            entryAddrs.add(cursor);
            cursor = cursor.add(stride);
        }

        Address tableStart = entryAddrs.get(0);
        Address tableEnd = entryAddrs.get(entryAddrs.size() - 1);

        try (PrintWriter w = new PrintWriter(new FileWriter(outPath))) {
            w.println("Table spans " + tableStart + " to " + tableEnd + " (" + entries.size() + " entries)");
            w.println();
            for (int i = 0; i < entries.size(); i++) {
                w.println("  [" + entryAddrs.get(i) + "] " + entries.get(i));
            }

            w.println();
            w.println("=== References TO table start " + tableStart + " ===");
            printRefs(w, tableStart);
            w.println();
            w.println("=== References TO table start - 4 (possible count-prefixed base) " + tableStart.subtract(4) + " ===");
            printRefs(w, tableStart.subtract(4));
        }
        println("Wrote report to " + outPath);
    }

    void printRefs(PrintWriter w, Address target) {
        FunctionManager fm = currentProgram.getFunctionManager();
        ReferenceManager refMgr = currentProgram.getReferenceManager();
        ReferenceIterator refs = refMgr.getReferencesTo(target);
        int count = 0;
        while (refs.hasNext()) {
            Reference ref = refs.next();
            count++;
            Address from = ref.getFromAddress();
            Function func = fm.getFunctionContaining(from);
            w.println("  from " + from + (func != null ? " IN FUNCTION " + func.getName() + "@" + func.getEntryPoint() : " IN DATA"));
        }
        w.println("  (" + count + " references)");
    }
}
