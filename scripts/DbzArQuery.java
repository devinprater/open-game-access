// Find the DBZ-AR menu/message functions via their log strings.
//
// Run:
//   analyzeHeadless <proj> DBZAR -process EBOOT.dec -noanalysis
//     -scriptPath <dir> -postScript DbzArQuery.java
//
// NOTE: Ghidra headless scripts must be JAVA here (PyGhidra has no cp314
// wheel). `Address` lives in ghidra.program.model.address; `Data` lives in
// ghidra.program.model.listing. Both imports are required.

import ghidra.app.script.GhidraScript;
// KEEP: Address IS used explicitly below (getFromAddress).
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.*;

public class DbzArQuery extends GhidraScript {

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

        // ---- 1. menu/message/log strings and who references them ------------
        String[] wanted = { "[MENU]", "[TITLE]", "[SHOP]", "MSG_SYS_", "MSG_HLP_",
            "[SYS] AUTO SAVE", "CURSOR", "sceLibFont" };
        emit("=== strings of interest and who references them ===");
        int shown = 0;
        for (Data d : listing.getDefinedData(true)) {
            Object v = d.getValue();
            if (v == null) continue;
            String s = v.toString();
            if (s.length() < 4 || s.length() > 200) continue;
            boolean interesting = false;
            for (String w : wanted) {
                if (s.contains(w)) { interesting = true; break; }
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
            if (++shown >= 40) break;
        }
        if (shown == 0) emit("  none — strings may sit in undefined bytes");
        emit("");

        // ---- 2. function census ---------------------------------------------
        int fcount = 0;
        for (Function f : fm.getFunctions(true)) fcount++;
        emit("=== functions recovered: " + fcount + " ===");

        String outPath = System.getenv("DBZAR_QUERY_OUT");
        if (outPath == null || outPath.isEmpty())
            outPath = "C:\\Users\\Devin Prater\\AppData\\Local\\Temp\\dbz-ar-extract\\dbzar-query-out.txt";
        PrintWriter pw = new PrintWriter(new FileWriter(outPath));
        for (String s : out) pw.println(s);
        pw.close();
        println("wrote " + out.size() + " lines to " + outPath);
    }
}
