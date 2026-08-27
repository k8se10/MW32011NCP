// FindSymbolByName.java — searches the symbol table for names containing a substring
// (case-insensitive), printing name + address + symbol type. Useful for locating import
// thunks (e.g. SetUnhandledExceptionFilter) by name when only a rough name is known.
//
// Usage: -postScript FindSymbolByName.java <output_path> <substring>

import ghidra.app.script.GhidraScript;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolIterator;
import ghidra.program.model.symbol.SymbolTable;

import java.io.FileWriter;
import java.io.PrintWriter;

public class FindSymbolByName extends GhidraScript {

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args == null || args.length < 2) {
            println("Usage: FindSymbolByName.java <output_path> <substring>");
            return;
        }
        String outPath = args[0];
        String needle = args[1].toLowerCase();

        SymbolTable st = currentProgram.getSymbolTable();
        SymbolIterator it = st.getSymbolIterator();

        try (PrintWriter w = new PrintWriter(new FileWriter(outPath))) {
            w.println("Symbols containing \"" + args[1] + "\":");
            int count = 0;
            while (it.hasNext()) {
                Symbol s = it.next();
                String name = s.getName();
                if (name.toLowerCase().contains(needle)) {
                    w.println("  " + name + " @ " + s.getAddress() + "  [" + s.getSymbolType() + "]" +
                        (s.isExternal() ? " EXTERNAL" : ""));
                    count++;
                }
            }
            w.println("Total: " + count);
        }
        println("Wrote report to " + outPath);
    }
}
