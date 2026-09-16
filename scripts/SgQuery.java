// Query the Steins;Gate EBOOT project for the script loader.
//
// WHY JAVA AND NOT PYTHON: headless Ghidra needs PyGhidra to run .py scripts, and
// PyGhidra does not install on this machine's Python 3.14. A Java script is Ghidra's
// native scripting language and needs nothing extra.
//
// Run:
//   analyzeHeadless <proj> SGProj -process EBOOT.dec -noanalysis \
//     -scriptPath <dir> -postScript SgQuery.java

import ghidra.app.script.GhidraScript;
// ⛔ `Address` LIVES IN ghidra.program.model.address AND IS USED EXPLICITLY BELOW.
// Removing the import (as a "cleanup" earlier) broke the build with "cannot find
// symbol: class Address" — so keep both this and the Data import out of listing.*.
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.*;

public class SgQuery extends GhidraScript {

    private List<String> out = new ArrayList<>();
    private void emit(String s) { out.add(s); }

    @Override
    public void run() throws Exception {
        Program prog = currentProgram;
        Listing listing = prog.getListing();
        FunctionManager fm = prog.getFunctionManager();

        emit("=== PROGRAM ===");
        emit("  " + prog.getName() + "  imageBase=" + prog.getImageBase());
        emit("  language " + prog.getLanguage().getLanguageID());
        emit("");

        // ---- 1. defined strings that matter, and their references -----------
        String[] wanted = { "SYSTEM.CFG", "SYSTEM.DAT", "system.cfg", "CPK", "error load" };
        emit("=== strings of interest and who references them ===");
        int shown = 0;
        for (Data d : listing.getDefinedData(true)) {
            Object v = d.getValue();
            if (v == null) continue;
            String s = v.toString();
            if (s.length() < 4 || s.length() > 200) continue;
            boolean interesting = false;
            for (String w : wanted) {
                if (s.toLowerCase().contains(w.toLowerCase())) { interesting = true; break; }
            }
            if (!interesting) continue;
            emit("  " + d.getAddress() + "  \"" + s.substring(0, Math.min(70, s.length())) + "\"");
            int n = 0;
            ReferenceIterator it = prog.getReferenceManager().getReferencesTo(d.getAddress());
            for (Reference r : it) {
                Address from = r.getFromAddress();
                Function fn = fm.getFunctionContaining(from);
                emit("      <- " + from + "  " + (fn != null ? fn.getName() : "(no function)"));
                if (++n >= 6) break;
            }
            if (n == 0) emit("      (no direct references)");
            if (++shown >= 25) break;
        }
        if (shown == 0) emit("  none — strings may sit in undefined bytes");
        emit("");

        // ---- 2. what functions exist ----------------------------------------
        int fcount = 0;
        for (Function f : fm.getFunctions(true)) fcount++;
        emit("=== functions recovered: " + fcount + " ===");
        emit("");

        // ---- 3. candidate loaders: functions referencing a cfg/cpk string ----
        emit("=== functions referencing a cfg/cpk-ish string ===");
        Set<Long> seen = new HashSet<>();
        for (Data d : listing.getDefinedData(true)) {
            Object v = d.getValue();
            if (v == null) continue;
            String s = v.toString().toLowerCase();
            if (!(s.contains("cfg") || s.contains("cpk") || s.contains(".dat"))) continue;
            ReferenceIterator it = prog.getReferenceManager().getReferencesTo(d.getAddress());
            for (Reference r : it) {
                Function fn = fm.getFunctionContaining(r.getFromAddress());
                if (fn == null) continue;
                long ep = fn.getEntryPoint().getOffset();
                if (seen.add(ep)) {
                    String nm = d.getValue().toString();
                    emit("  " + fn.getEntryPoint() + "  " + fn.getName() + "   (via \"" +
                         nm.substring(0, Math.min(40, nm.length())) + "\")");
                }
            }
        }
        emit("  " + seen.size() + " candidates");
        emit("");

        // ---- 4. does the executable contain the script text? ----------------
        emit("=== does this binary contain script text? ===");
        String[] probes = { "Rintaro", "Mayuri", "paper cups" };
        for (String p : probes) {
            int c = 0;
            for (Data d : listing.getDefinedData(true)) {
                Object v = d.getValue();
                if (v != null && v.toString().contains(p)) {
                    c++;
                    if (c <= 2) emit("  " + d.getAddress() + " \"" +
                                     v.toString().substring(0, Math.min(60, v.toString().length())) + "\"");
                }
            }
            emit("  \"" + p + "\": " + c + " hits");
        }
        emit("");

        // ---- 5. count xrefs to the SYSTEM.CFG / SYSTEM.DAT strings ----------
        // These are the entries into the config loader, which is where a script
        // table would be parsed. Report the calling functions with a size hint.
        emit("=== entry points into the config loader ===");
        for (Data d : listing.getDefinedData(true)) {
            Object v = d.getValue();
            if (v == null) continue;
            String s = v.toString();
            if (!s.equals("SYSTEM.CFG") && !s.equals("SYSTEM.DAT") &&
                !s.contains("error load system.cfg")) continue;
            emit("  string " + d.getAddress() + " \"" + s + "\"");
            ReferenceIterator it = prog.getReferenceManager().getReferencesTo(d.getAddress());
            for (Reference r : it) {
                Function fn = fm.getFunctionContaining(r.getFromAddress());
                if (fn != null) {
                    emit("    called from " + fn.getEntryPoint() + " " + fn.getName() +
                         "  (body " + fn.getBody().getNumAddresses() + " bytes)");
                } else {
                    emit("    ref from " + r.getFromAddress() + " (no function)");
                }
            }
        }

        // write it out so the result survives the console being truncated
        try (PrintWriter pw = new PrintWriter(new FileWriter(
                "C:\\Users\\Devin Prater\\AppData\\Local\\Temp\\sg-query-out.txt"))) {
            for (String s : out) pw.println(s);
        }
        for (String s : out) println(s);
    }
}
