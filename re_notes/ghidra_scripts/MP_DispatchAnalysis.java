import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.listing.Function;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.mem.Memory;
import java.io.PrintWriter;
import java.io.FileWriter;

public class MP_DispatchAnalysis extends GhidraScript {
    @Override
    public void run() throws Exception {
        DecompInterface decompiler = new DecompInterface();
        decompiler.openProgram(currentProgram);
        
        try (PrintWriter w = new PrintWriter(new FileWriter("C:\\tmp\\mp_dispatch_analysis.txt"))) {
            w.println("=== iw5mp.exe MP Dispatch Analysis ===\n");
            
            // Analyze FUN_005a3960 (dispatch candidate)
            w.println("## FUN_005a3960 (Dispatch Handler Candidate)\n");
            Address addr1 = currentProgram.getAddressFactory().getAddress("0x005a3960");
            Function func1 = currentProgram.getFunctionManager().getFunctionContaining(addr1);
            if (func1 != null) {
                w.println(String.format("Function: %s @ %s", func1.getName(), func1.getEntryPoint()));
                w.println(String.format("Size: 0x%x bytes\n", func1.getBody().getNumAddresses()));
                
                DecompileResults result1 = decompiler.decompileFunction(func1, 60, monitor);
                String decompile1 = result1.getDecompiledFunction().getC();
                w.println("Decompilation:\n");
                w.println(decompile1);
                w.println("\n" + "=".repeat(80) + "\n");
            } else {
                w.println("ERROR: Function not found at 0x005a3960\n");
            }
            
            // Analyze FUN_0048c1c0 (lookup/resolver)
            w.println("## FUN_0048c1c0 (Bind Lookup Function)\n");
            Address addr2 = currentProgram.getAddressFactory().getAddress("0x0048c1c0");
            Function func2 = currentProgram.getFunctionManager().getFunctionContaining(addr2);
            if (func2 != null) {
                w.println(String.format("Function: %s @ %s", func2.getName(), func2.getEntryPoint()));
                w.println(String.format("Size: 0x%x bytes\n", func2.getBody().getNumAddresses()));
                
                DecompileResults result2 = decompiler.decompileFunction(func2, 60, monitor);
                String decompile2 = result2.getDecompiledFunction().getC();
                w.println("Decompilation:\n");
                w.println(decompile2);
                w.println("\n" + "=".repeat(80) + "\n");
            } else {
                w.println("ERROR: Function not found at 0x0048c1c0\n");
            }
            
            // Dump raw bind-name table
            w.println("## Bind-Name Table @ 0x008aa3bc\n");
            Memory mem = currentProgram.getMemory();
            int entryCount = 0;
            for (long offset = 0; offset < 0x134; offset += 8) {
                try {
                    Address entryAddr = currentProgram.getAddressFactory().getAddress("0x008aa3bc").add(offset);
                    long dword = mem.getInt(entryAddr) & 0xFFFFFFFFL;
                    
                    // Try to read as string pointer
                    Address strPtr = currentProgram.getAddressFactory().getAddress("0x" + String.format("%08x", dword));
                    byte[] strBytes = new byte[64];
                    try {
                        mem.getBytes(strPtr, strBytes);
                        String str = "";
                        for (int i = 0; i < 64 && strBytes[i] != 0; i++) {
                            str += (char) strBytes[i];
                        }
                        w.println(String.format("[%02d] @ 0x%08x: \"%s\"", entryCount, dword, str));
                    } catch (Exception e) {
                        w.println(String.format("[%02d] @ 0x%08x: (invalid pointer)", entryCount, dword));
                    }
                    entryCount++;
                } catch (Exception e) {
                    break;
                }
            }
            w.println(String.format("\nTotal entries found: %d\n", entryCount));
            
            // Extract function prologues for pattern scanning
            w.println("## Function Prologues (for pattern scanning)\n");
            w.println("FUN_005a3960 prologue (first 32 bytes):\n");
            Address prologue1 = currentProgram.getAddressFactory().getAddress("0x005a3960");
            byte[] bytes1 = new byte[32];
            mem.getBytes(prologue1, bytes1);
            w.print("  ");
            for (byte b : bytes1) {
                w.print(String.format("%02x ", b & 0xFF));
            }
            w.println("\n");
            
            w.println("FUN_0048c1c0 prologue (first 32 bytes):\n");
            Address prologue2 = currentProgram.getAddressFactory().getAddress("0x0048c1c0");
            byte[] bytes2 = new byte[32];
            mem.getBytes(prologue2, bytes2);
            w.print("  ");
            for (byte b : bytes2) {
                w.print(String.format("%02x ", b & 0xFF));
            }
            w.println("\n");
            
            w.println("=== Analysis Complete ===");
            
        } finally {
            decompiler.closeProgram();
        }
    }
}
