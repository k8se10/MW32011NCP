// FindGlobalRefs.java — Ghidra headless GhidraScript.
// Given a list of hex global-data addresses (the resolved cvar_t*/float* storage
// globals found from decompiling the cvar-registration function), finds every
// function that reads/writes each one and ranks functions by how many distinct
// target globals they touch. A function touching cl_yawspeed + cl_pitchspeed +
// m_pitch + m_yaw + cl_anglespeedkey together is a strong CL_AdjustAngles/usercmd-
// angle-update candidate.
//
// Usage: -postScript FindGlobalRefs.java <output_path> <label1> <addr1> [<label2> <addr2> ...]
//   e.g. -postScript FindGlobalRefs.java out.txt cl_yawspeed 00a98ac0 m_pitch 00aa4084
// (alternating label/address args, NOT "label=addr" — the headless launcher's arg
// tokenizer splits on "=" before scripts ever see it, silently breaking that form.)

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.symbol.ReferenceManager;

import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.*;

public class FindGlobalRefs extends GhidraScript {

    static class Hit {
        Function func;
        Set<String> labels = new TreeSet<>();
    }

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args == null || args.length < 3) {
            println("Usage: FindGlobalRefs.java <output_path> <label> <addr> ...");
            return;
        }
        String outPath = args[0];

        FunctionManager fm = currentProgram.getFunctionManager();
        ReferenceManager refMgr = currentProgram.getReferenceManager();

        Map<Address, Hit> funcHits = new HashMap<>();
        List<String> raw = new ArrayList<>();

        for (int i = 1; i + 1 < args.length; i += 2) {
            String label = args[i];
            String addrStr = args[i + 1];
            Address target = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(addrStr);

            ReferenceIterator refs = refMgr.getReferencesTo(target);
            int count = 0;
            while (refs.hasNext()) {
                Reference ref = refs.next();
                count++;
                Address fromAddr = ref.getFromAddress();
                Function func = fm.getFunctionContaining(fromAddr);
                if (func == null) continue;
                Address key = func.getEntryPoint();
                Hit hit = funcHits.get(key);
                if (hit == null) {
                    hit = new Hit();
                    hit.func = func;
                    funcHits.put(key, hit);
                }
                hit.labels.add(label);
            }
            raw.add(label + " (" + addrStr + "): " + count + " references");
        }

        List<Hit> ranked = new ArrayList<>(funcHits.values());
        ranked.sort((a, b) -> b.labels.size() - a.labels.size());

        try (PrintWriter w = new PrintWriter(new FileWriter(outPath))) {
            w.println("=== Reference counts per global ===");
            for (String s : raw) w.println(s);

            w.println();
            w.println("=== Candidate functions, ranked by distinct global diversity (" + ranked.size() + ") ===");
            for (Hit hit : ranked) {
                w.println();
                w.println("---- " + hit.func.getName() + " @ " + hit.func.getEntryPoint()
                    + " (globals: " + String.join(", ", hit.labels) + ") ----");
            }
        }

        println("Wrote report to " + outPath);
    }
}
