// FindInputRefs.java — Ghidra headless GhidraScript (Java, since this Ghidra install
// doesn't have PyGhidra/Jython wired up).
//
// Scans every defined string in the program for substrings characteristic of IW
// engine's (Quake3-derived) input/usercmd/menu subsystem — movement key binds,
// mouse/look cvars, FOV cvars, and known UI cvars already confirmed via the earlier
// strings/import-table pass on iw5mp.exe. For each match, walks references TO that
// string and records which function each reference lives in, then writes a report
// ranking candidate functions by how many distinct target strings they touch.
//
// Run against an already-imported/analyzed program with:
//   analyzeHeadless.bat <project_dir> <project_name> -process <program_name> -noanalysis
//     -scriptPath <this_dir> -postScript FindInputRefs.java <output_report_path>

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.DataIterator;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.symbol.ReferenceManager;

import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.*;

public class FindInputRefs extends GhidraScript {

    static final String[] TARGETS = {
        "+forward", "+back", "+moveleft", "+moveright", "+moveup", "+movedown",
        "+speed", "+strafe", "+attack", "+attack2", "+use", "+leanleft", "+leanright",
        "+prone", "+breath_sprint", "+sprint", "+smoke",
        "cl_forwardspeed", "cl_sidespeed", "cl_backspeed", "cl_movespeedscale", "cl_run",
        "cl_anglespeedkey",
        "sensitivity", "m_pitch", "m_yaw", "m_forward", "m_side", "m_filter", "in_mouse",
        "cl_yawspeed", "cl_pitchspeed", "cl_maxpackets",
        "cg_fov", "cg_fovmin", "cg_fovscale",
        "usercmd", "forwardmove", "rightmove", "viewangles", "anglemove",
        "ui_mousepitch", "ui_cursor", "ui_active",
    };

    static class Hit {
        Function func;
        Set<String> targets = new TreeSet<>();
        Set<String> strings = new TreeSet<>();
    }

    @Override
    protected void run() throws Exception {
        Listing listing = currentProgram.getListing();
        FunctionManager fm = currentProgram.getFunctionManager();
        ReferenceManager refMgr = currentProgram.getReferenceManager();

        String[] args = getScriptArgs();
        String outPath = (args != null && args.length > 0) ? args[0]
            : "D:\\Tools\\ghidra_projects\\input_refs_report.txt";

        List<String> rawHits = new ArrayList<>();
        Map<Address, Hit> funcHits = new HashMap<>();

        DataIterator it = listing.getDefinedData(true);
        while (it.hasNext()) {
            Data data = it.next();
            if (!data.hasStringValue()) continue;
            Object val;
            try {
                val = data.getValue();
            } catch (Exception e) {
                continue;
            }
            if (val == null) continue;
            String sval = val.toString();
            String lower = sval.toLowerCase();

            for (String target : TARGETS) {
                if (lower.contains(target)) {
                    rawHits.add("[" + target + "] " + data.getAddress() + " : " + sval);
                    ReferenceIterator refs = refMgr.getReferencesTo(data.getAddress());
                    while (refs.hasNext()) {
                        Reference ref = refs.next();
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
                        hit.targets.add(target);
                        hit.strings.add(sval);
                    }
                    break; // one match per string is enough
                }
            }
        }

        List<Hit> ranked = new ArrayList<>(funcHits.values());
        ranked.sort((a, b) -> b.targets.size() - a.targets.size());

        try (PrintWriter w = new PrintWriter(new FileWriter(outPath))) {
            w.println("=== Raw string hits (" + rawHits.size() + ") ===");
            for (String s : rawHits) w.println(s);

            w.println();
            w.println("=== Candidate functions, ranked by distinct target-string diversity (" + ranked.size() + ") ===");
            for (Hit hit : ranked) {
                w.println();
                w.println("---- " + hit.func.getName() + " @ " + hit.func.getEntryPoint()
                    + " (targets: " + String.join(", ", hit.targets) + ") ----");
                for (String s : hit.strings) {
                    w.println("    string: " + s);
                }
            }
        }

        println("Wrote report to " + outPath + " (" + rawHits.size() + " string hits, " + ranked.size() + " candidate functions)");
    }
}
