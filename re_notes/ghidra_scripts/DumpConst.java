import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.mem.Memory;

public class DumpConst extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        Memory mem = currentProgram.getMemory();
        for (String hex : args) {
            Address addr = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(hex);
            try {
                int i = mem.getInt(addr);
                float f = Float.intBitsToFloat(i);
                println(hex + " -> float=" + f + " int=" + i);
            } catch (Exception e) {
                println(hex + " -> ERROR " + e.getMessage());
            }
        }
    }
}
