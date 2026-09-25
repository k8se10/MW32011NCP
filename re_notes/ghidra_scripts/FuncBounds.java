import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import java.io.FileWriter;
import java.io.PrintWriter;

public class FuncBounds extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        String outPath = args[0];
        try (PrintWriter w = new PrintWriter(new FileWriter(outPath))) {
            for (int i = 1; i < args.length; i++) {
                Address addr = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(args[i]);
                Function f = currentProgram.getFunctionManager().getFunctionContaining(addr);
                if (f == null) { w.println(args[i] + " -> no function"); continue; }
                w.println(f.getName() + " entry=" + f.getEntryPoint() + " body=" + f.getBody());
            }
        }
    }
}
