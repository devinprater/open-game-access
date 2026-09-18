// Batch 3: find the battle damage path via the "%05ddmg" / "%02dHIT" format
// strings and the [BTL] log tags.
//
// Run:
//   analyzeHeadless <proj> DBZAR -process EBOOT.dec -noanalysis
//     -scriptPath <dir> -postScript DbzArBtl.java

import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.*;

public class DbzArBtl extends GhidraScript {

    private List<String> out = new ArrayList<>();
    private void emit(String s) { out.add(s); }

    @Override
    public void run() throws Exception {
        Program prog = currentProgram;
        Listing listing = prog.getListing();
        FunctionManager fm = prog.getFunctionManager();

        String[] wanted = { "dmg", "HIT", "[BTL]", "HITSTOP" };
        emit("=== battle strings and who references them ===");
        for (Data d : listing.getDefinedData(true)) {
            Object v = d.getValue();
            if (v == null) continue;
            String s = v.toString();
            if (s.length() < 3 || s.length() > 120) continue;
            boolean interesting = false;
            for (String w : wanted) {
                if (s.contains(w)) { interesting = true; break; }
            }
            if (!interesting) continue;
            emit("  " + d.getAddress() + "  \"" + s.substring(0, Math.min(60, s.length())).replace("\n","\\n") + "\"");
            int n = 0;
            ReferenceIterator it = prog.getReferenceManager().getReferencesTo(d.getAddress());
            for (Reference r : it) {
                Address from = r.getFromAddress();
                Function fn = fm.getFunctionContaining(from);
                emit("      <- " + from + "  " + (fn != null ? fn.getName() + " size=" + fn.getBody().getNumAddresses() : "(no function)"));
                if (++n >= 8) break;
            }
            if (n == 0) emit("      (no direct references)");
        }

        String outPath = System.getenv("DBZAR_BTL_OUT");
        if (outPath == null || outPath.isEmpty())
            outPath = "C:\\Users\\Devin Prater\\AppData\\Local\\Temp\\dbz-ar-extract\\dbzar-btl-out.txt";
        PrintWriter pw = new PrintWriter(new FileWriter(outPath));
        for (String s : out) pw.println(s);
        pw.close();
        println("wrote " + out.size() + " lines to " + outPath);
    }
}
