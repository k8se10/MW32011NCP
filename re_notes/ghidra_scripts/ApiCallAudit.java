// ApiCallAudit.java — Ghidra headless GhidraScript (Java).
//
// Inventories every call site to a fixed list of process/module/memory-introspection
// Windows APIs in the currently loaded program. For each match found among imported
// functions (by name), walks all references TO it, records the calling function and
// a short disassembly snippet around the call site, and writes a report.
//
// Run against an already-imported/analyzed program with:
//   analyzeHeadless.bat <project_dir> <project_name> -process <program_name> -noanalysis
//     -scriptPath <this_dir> -postScript ApiCallAudit.java <output_report_path>

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.symbol.ReferenceManager;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolIterator;
import ghidra.program.model.symbol.SymbolTable;
import ghidra.program.model.symbol.SymbolType;

import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.*;

public class ApiCallAudit extends GhidraScript {

    static final String[] TARGET_APIS = {
        "CreateToolhelp32Snapshot", "Module32First", "Module32FirstW", "Module32Next", "Module32NextW",
        "Process32First", "Process32FirstW", "Process32Next", "Process32NextW",
        "EnumProcessModules", "EnumProcessModulesEx", "EnumProcesses",
        "GetModuleFileNameA", "GetModuleFileNameW", "GetModuleFileNameExA", "GetModuleFileNameExW",
        "VirtualQuery", "VirtualQueryEx", "ReadProcessMemory", "WriteProcessMemory",
        "OpenProcess", "NtQuerySystemInformation", "NtQueryInformationProcess",
        "GetModuleHandleA", "GetModuleHandleW", "GetModuleHandleExA", "GetModuleHandleExW",
        "GetProcAddress", "LoadLibraryA", "LoadLibraryW", "LoadLibraryExA", "LoadLibraryExW",
        "CreateFileA", "CreateFileW", // included to catch lockfile-style single-instance checks for comparison
    };

    @Override
    protected void run() throws Exception {
        Listing listing = currentProgram.getListing();
        FunctionManager fm = currentProgram.getFunctionManager();
        ReferenceManager refMgr = currentProgram.getReferenceManager();
        SymbolTable symTab = currentProgram.getSymbolTable();

        String[] args = getScriptArgs();
        String outPath = (args != null && args.length > 0) ? args[0]
            : "D:\\Tools\\ghidra_projects\\api_call_audit_report.txt";
        boolean append = (args != null && args.length > 1 && "append".equals(args[1]));

        StringBuilder sb = new StringBuilder();
        sb.append("=== ApiCallAudit for ").append(currentProgram.getName()).append(" ===\n\n");

        // Full KERNEL32 import listing for reference
        sb.append("--- Imported symbols referencing KERNEL32/NTDLL (name contains 'Kernel32' or 'ntdll' as library, informational) ---\n");
        SymbolIterator allSyms = symTab.getSymbolIterator();
        int importCount = 0;
        while (allSyms.hasNext()) {
            Symbol s = allSyms.next();
            if (s.getSymbolType() == SymbolType.FUNCTION && s.isExternal()) {
                importCount++;
            }
        }
        sb.append("Total external function symbols in program: ").append(importCount).append("\n\n");

        for (String api : TARGET_APIS) {
            sb.append("=== API: ").append(api).append(" ===\n");
            List<Symbol> matches = new ArrayList<>();
            SymbolIterator it = symTab.getSymbols(api);
            while (it.hasNext()) matches.add(it.next());
            // also try globalSymbolMap-ish lookup via getGlobalSymbols
            for (Symbol s : symTab.getGlobalSymbols(api)) {
                if (!matches.contains(s)) matches.add(s);
            }

            if (matches.isEmpty()) {
                sb.append("  NOT FOUND (not imported / no symbol named exactly this) — negative result.\n\n");
                continue;
            }

            for (Symbol sym : matches) {
                Address addr = sym.getAddress();
                sb.append("  Symbol: ").append(sym.getName()).append(" @ ").append(addr)
                  .append(" (external=").append(sym.isExternal()).append(")\n");

                ReferenceIterator refs = refMgr.getReferencesTo(addr);
                int callSiteCount = 0;
                while (refs.hasNext()) {
                    Reference ref = refs.next();
                    Address fromAddr = ref.getFromAddress();
                    Function callerFunc = fm.getFunctionContaining(fromAddr);
                    callSiteCount++;
                    sb.append("    call site #").append(callSiteCount).append(": ").append(fromAddr);
                    if (callerFunc != null) {
                        sb.append("  in function ").append(callerFunc.getName())
                          .append(" @ ").append(callerFunc.getEntryPoint());
                    } else {
                        sb.append("  (no containing function found)");
                    }
                    sb.append("\n");
                }
                if (callSiteCount == 0) {
                    sb.append("    (imported but zero references found — dead import or thunk-only)\n");
                }
            }
            sb.append("\n");
        }

        try (PrintWriter w = new PrintWriter(new FileWriter(outPath, append))) {
            w.print(sb.toString());
        }

        println("Wrote report to " + outPath);
    }
}
