// Decompile the Steins;Gate config loader and follow what it builds.
//
// WHY THIS FUNCTION: FUN_0000b990 references "SYSTEM.CFG" and "error load system.cfg",
// so it parses the game's own configuration. A config that names scripts is the most
// likely place to find how the game indexes them, and therefore how a "current script
// position" would be represented.
//
// Run:
//   analyzeHeadless <proj> SGProj -process EBOOT.dec -noanalysis \
//     -scriptPath <dir> -postScript SgDecomp.java 0xb990 0x93bf8 0xa0bfc

import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.*;

public class SgDecomp extends GhidraScript {

    private List<String> out = new ArrayList<>();
    private void emit(String s) { out.add(s); }

    @Override
    public void run() throws Exception {
        Program prog = currentProgram;
        FunctionManager fm = prog.getFunctionManager();
        DecompInterface di = new DecompInterface();
        di.openProgram(prog);

        String[] args = getScriptArgs();
        List<Long> targets = new ArrayList<>();
        for (String a : args) {
            try { targets.add(Long.decode(a)); } catch (Exception e) { /* ignore */ }
        }
        if (targets.isEmpty()) {
            targets.add(0xb990L);
            targets.add(0x93bf8L);
            targets.add(0xa0bfcL);
        }

        emit("=== program " + prog.getName() + " base " + prog.getImageBase() + " ===");
        emit("");

        for (Long t : targets) {
            Address ep = prog.getAddressFactory().getDefaultAddressSpace().getAddress(t);
            Function f = fm.getFunctionAt(ep);
            emit("################ FUNCTION " + ep + " ################");
            if (f == null) {
                emit("  no function at " + ep);
                emit("");
                continue;
            }
            emit("  name=" + f.getName() + "  body=" + f.getBody().getNumAddresses() + " bytes");
            emit("  signature: " + f.getPrototypeString(false, false));
            emit("");

            // ---- callers -----------------------------------------------------
            emit("  --- callers ---");
            int cn = 0;
            for (Function c : f.getCallingFunctions(monitor)) {
                emit("    " + c.getEntryPoint() + "  " + c.getName());
                if (++cn >= 12) break;
            }
            if (cn == 0) emit("    (none found)");
            emit("");

            // ---- callees -----------------------------------------------------
            emit("  --- callees ---");
            Set<Long> seen = new TreeSet<>();
            InstructionIterator ii = prog.getListing().getInstructions(f.getBody(), true);
            for (Instruction ins : ii) {
                if (!ins.getFlowType().isCall()) continue;
                Address[] flows = ins.getFlows();
                for (Address a : flows) {
                    Function cf = fm.getFunctionAt(a);
                    if (cf != null) seen.add(cf.getEntryPoint().getOffset());
                }
            }
            for (Long d : seen) {
                Function cf = fm.getFunctionAt(prog.getAddressFactory()
                        .getDefaultAddressSpace().getAddress(d));
                emit("    " + cf.getEntryPoint() + "  " + cf.getName());
            }
            if (seen.isEmpty()) emit("    (no direct calls resolved)");
            emit("");

            // ---- referenced strings ------------------------------------------
            emit("  --- strings referenced in this function ---");
            int sn = 0;
            InstructionIterator ii2 = prog.getListing().getInstructions(f.getBody(), true);
            for (Instruction ins : ii2) {
                for (Reference r : ins.getReferencesFrom()) {
                    Data d = prog.getListing().getDataAt(r.getToAddress());
                    if (d == null || d.getValue() == null) continue;
                    String s = d.getValue().toString();
                    if (s.length() < 3 || s.length() > 90) continue;
                    boolean printable = true;
                    for (char c : s.toCharArray()) {
                        if (c < 0x20 || c > 0x7e) { printable = false; break; }
                    }
                    if (!printable) continue;
                    emit("    \"" + s + "\"");
                    if (++sn >= 30) break;
                }
                if (sn >= 30) break;
            }
            if (sn == 0) emit("    (none)");
            emit("");

            // ---- decompilation -----------------------------------------------
            DecompileResults res = di.decompileFunction(f, 60, monitor);
            emit("  --- decompiled ---");
            if (res != null && res.decompileCompleted()) {
                String c = res.getDecompiledFunction().getC();
                emit(c);
            } else {
                emit("  DECOMPILE FAILED: " + (res != null ? res.getErrorMessage() : "null"));
            }
            emit("");
            emit("");
        }

        try (PrintWriter pw = new PrintWriter(new FileWriter(
                "C:\\Users\\Devin Prater\\AppData\\Local\\Temp\\sg-decomp-out.txt"))) {
            for (String s : out) pw.println(s);
        }
        println("wrote " + out.size() + " lines to sg-decomp-out.txt");
    }
}
