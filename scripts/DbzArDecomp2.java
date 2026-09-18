// Batch 2: decompile the MENU handlers + the task-system primitives.
//
// Run:
//   analyzeHeadless <proj> DBZAR -process EBOOT.dec -noanalysis
//     -scriptPath <dir> -postScript DbzArDecomp2.java

import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.*;

public class DbzArDecomp2 extends GhidraScript {

    private List<String> out = new ArrayList<>();
    private void emit(String s) { out.add(s); }

    private void dumpFunc(FunctionManager fm, DecompInterface di, String addrStr, int maxLines) {
        try {
            Address a = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(addrStr);
            Function fn = fm.getFunctionAt(a);
            if (fn == null) { emit("--- " + addrStr + " NOT A FUNCTION ---"); return; }
            emit("=== " + fn.getName() + " @ " + fn.getEntryPoint() + " size=" + fn.getBody().getNumAddresses() + " ===");
            emit("--- callers (max 10) ---");
            int n = 0;
            for (Reference r : currentProgram.getReferenceManager().getReferencesTo(fn.getEntryPoint())) {
                Function cf = fm.getFunctionContaining(r.getFromAddress());
                emit("    <- " + r.getFromAddress() + "  " + (cf != null ? cf.getName() : "(none)"));
                if (++n >= 10) break;
            }
            emit("--- callees ---");
            Set<Function> called = new HashSet<>();
            for (Instruction ins : currentProgram.getListing().getInstructions(fn.getBody(), true)) {
                for (Reference r : ins.getReferencesFrom()) {
                    if (r.getReferenceType().isCall()) {
                        Function cf = fm.getFunctionAt(r.getToAddress());
                        if (cf != null && called.add(cf))
                            emit("    -> " + cf.getName() + " @ " + cf.getEntryPoint());
                    }
                }
            }
            emit("--- decompile (first " + maxLines + " lines) ---");
            di.openProgram(currentProgram);
            DecompileResults res = di.decompileFunction(fn, 60, monitor);
            if (res != null && res.decompileCompleted()) {
                String[] lines = res.getDecompiledFunction().getC().split("\n");
                for (int i = 0; i < Math.min(lines.length, maxLines); i++) emit("    " + lines[i]);
                if (lines.length > maxLines) emit("    ... (" + (lines.length - maxLines) + " more lines)");
            } else {
                emit("    (decompile failed)");
            }
            emit("");
        } catch (Exception e) {
            emit("--- " + addrStr + " ERROR " + e.getMessage() + " ---");
        }
    }

    @Override
    public void run() throws Exception {
        FunctionManager fm = currentProgram.getFunctionManager();
        DecompInterface di = new DecompInterface();
        // MENU handlers
        dumpFunc(fm, di, "000e2374", 150);
        dumpFunc(fm, di, "000e2468", 150);
        dumpFunc(fm, di, "000e28b8", 120);
        // task-system primitives
        dumpFunc(fm, di, "001408a8", 100);
        dumpFunc(fm, di, "00140a30", 80);

        String outPath = System.getenv("DBZAR_DECOMP_OUT");
        if (outPath == null || outPath.isEmpty())
            outPath = "C:\\Users\\Devin Prater\\AppData\\Local\\Temp\\dbz-ar-extract\\dbzar-decomp2-out.txt";
        PrintWriter pw = new PrintWriter(new FileWriter(outPath));
        for (String s : out) pw.println(s);
        pw.close();
        println("wrote " + out.size() + " lines to " + outPath);
    }
}
