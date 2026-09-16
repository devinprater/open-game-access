// Find and decompile the code that references the message-log (backlog) strings.
//
// ⭐ WHY THESE STRINGS: the executable contains
//      "BACKLOG", "MesLog", "MesLogSave", "MessageLog Buf"
//      "MsgLog Load No:%d  Id:%d"      <-- NAMES THE VARIABLES THE BACKLOG READS
//   That last format string is the strongest lead in this whole investigation: a printf
//   that reports "No" and "Id" for a message-log load is the backlog's own loader, and
//   whatever it passes as No/Id IS the read position being sought.
//
// Run:
//   analyzeHeadless <proj> SGProj -process EBOOT.dec -noanalysis \
//     -scriptPath <dir> -postScript SgLog.java

import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.*;

public class SgLog extends GhidraScript {

    private List<String> out = new ArrayList<>();
    private void emit(String s) { out.add(s); }

    private static final String[] NEEDLES = {
        "MsgLog Load No", "MessageLog Buf", "MesLogSave", "MesLog", "BACKLOG"
    };

    @Override
    public void run() throws Exception {
        Program prog = currentProgram;
        Listing listing = prog.getListing();
        FunctionManager fm = prog.getFunctionManager();
        DecompInterface di = new DecompInterface();
        di.openProgram(prog);

        emit("=== program " + prog.getName() + " ===");
        emit("");

        // ---- 1. locate each string and its referencing functions ---------------
        Set<Long> funcs = new LinkedHashSet<>();
        Map<Long, String> why = new HashMap<>();

        for (Data d : listing.getDefinedData(true)) {
            Object v = d.getValue();
            if (v == null) continue;
            String s = v.toString();
            String matched = null;
            for (String n : NEEDLES) {
                if (s.indexOf(n) >= 0) { matched = n; break; }
            }
            if (matched == null) continue;

            emit("string " + d.getAddress() + "  \"" +
                 s.substring(0, Math.min(60, s.length())) + "\"   [matched " + matched + "]");

            ReferenceIterator it = prog.getReferenceManager().getReferencesTo(d.getAddress());
            int n = 0;
            for (Reference r : it) {
                Function f = fm.getFunctionContaining(r.getFromAddress());
                if (f == null) {
                    emit("    ref from " + r.getFromAddress() + " (no function)");
                    continue;
                }
                long ep = f.getEntryPoint().getOffset();
                funcs.add(ep);
                why.putIfAbsent(ep, matched);
                emit("    <- " + f.getEntryPoint() + "  " + f.getName() +
                     "  (body " + f.getBody().getNumAddresses() + ")");
                n++;
            }
            if (n == 0) emit("    (no direct references)");
        }
        emit("");
        emit("=== " + funcs.size() + " distinct functions reference these strings ===");
        for (Long f : funcs) emit("  " + Long.toHexString(f) + "   via " + why.get(f));
        emit("");

        // ---- 2. decompile each, and list its own string references --------------
        for (Long ep : funcs) {
            Address a = prog.getAddressFactory().getDefaultAddressSpace().getAddress(ep);
            Function f = fm.getFunctionAt(a);
            if (f == null) continue;
            emit("################ " + f.getEntryPoint() + " " + f.getName() +
                 "  (" + f.getBody().getNumAddresses() + " bytes) ################");

            // strings it references
            Set<String> strs = new LinkedHashSet<>();
            InstructionIterator ii = listing.getInstructions(f.getBody(), true);
            for (Instruction ins : ii) {
                for (Reference r : ins.getReferencesFrom()) {
                    Data d = listing.getDataAt(r.getToAddress());
                    if (d == null || d.getValue() == null) continue;
                    String s = d.getValue().toString();
                    if (s.length() < 4 || s.length() > 80) continue;
                    boolean ok = true;
                    for (char c : s.toCharArray()) if (c < 0x20 || c > 0x7e) { ok = false; break; }
                    if (ok) strs.add(s);
                }
            }
            emit("  strings: " + strs);
            emit("");

            DecompileResults res = di.decompileFunction(f, 90, monitor);
            if (res != null && res.decompileCompleted()) {
                emit(res.getDecompiledFunction().getC());
            } else {
                emit("  DECOMPILE FAILED: " + (res != null ? res.getErrorMessage() : "null"));
            }
            emit("");
        }

        try (PrintWriter pw = new PrintWriter(new FileWriter(
                "C:\\Users\\Devin Prater\\AppData\\Local\\Temp\\sg-log-out.txt"))) {
            for (String s : out) pw.println(s);
        }
        println("wrote " + out.size() + " lines to sg-log-out.txt");
    }
}
