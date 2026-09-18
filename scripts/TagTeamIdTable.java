// TagTeamIdTable.java — find the code that READS the character/UI name id table.
//
// WHY: this is the step that closes character identity. Established (see the doc):
//   - names are NOT stored as string pointers; the game stores a MESSAGE ID and resolves text
//     through FUN_0883ee74(message_id, -1)
//   - the id tables are STATIC DATA, present in EBOOT.dec:
//        0x08A75538:  505 506 507 508 509 510 171 172 169 170 173 174 ...
//        0x08A72100: 1109 1109 1109 1109 1110 1111 1111 ...
//   - a default-implementation vtable sits at 0x08A75584 ({fnptr, 0} pairs; entries that are
//     "jr $ra; nop" are empty stubs)
//
// This script finds every instruction whose operand lies inside the id-table region and reports
// the containing function, then decompiles those functions. One of them maps a fighter slot to
// its name id -- that mapping IS the identity.
//
// MIPS note: a 32-bit constant is built with LUI+ORI/ADDIU pairs, so Ghidra's scalar operand on the
// LUI is the HIGH half. This script therefore reports BOTH the raw scalar and, for LUI, the
// reconstructed base so the table base is recognisable.

import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.scalar.Scalar;

import java.io.*;
import java.util.*;

public class TagTeamIdTable extends GhidraScript {

    // the id-table region Codex's slices pointed at
    static final long TBL_LO = 0x08A75000L;
    static final long TBL_HI = 0x08A76000L;
    // plus the other key sources the slices used
    static final long[] EXTRA = { 0x08A72100L, 0x08A80028L, 0x08A80090L, 0x08A80118L };

    StringBuilder out = new StringBuilder();
    DecompInterface dec = new DecompInterface();
    void log(String s) { out.append(s).append('\n'); }

    @Override
    public void run() throws Exception {
        dec.openProgram(currentProgram);

        log("=== TagTeamIdTable: who READS the name id table? ===");
        log("id-table region: 0x" + Long.toHexString(TBL_LO) + " - 0x" + Long.toHexString(TBL_HI));
        log("");

        Set<Function> funcs = new LinkedHashSet<>();
        Map<Function, List<String>> evidence = new LinkedHashMap<>();

        Listing lst = currentProgram.getListing();
        InstructionIterator it = lst.getInstructions(true);
        int n = 0;
        while (it.hasNext()) {
            Instruction ins = it.next();
            String mnem = ins.getMnemonicString();

            for (int op = 0; op < ins.getNumOperands(); op++) {
                Object[] objs = ins.getOpObjects(op);
                for (Object o : objs) {
                    if (!(o instanceof Scalar)) continue;
                    long v = ((Scalar) o).getUnsignedValue();

                    boolean inTbl = (v >= TBL_LO && v < TBL_HI);
                    boolean inExtra = false;
                    for (long e : EXTRA) if (v == e || (v >= e && v < e + 0x40)) inExtra = true;
                    if (!inTbl && !inExtra) continue;

                    // for LUI the value is the HIGH half shifted right 16 -> reconstruct
                    long base = v;
                    String note = "";
                    if (mnem.equalsIgnoreCase("lui")) {
                        base = (v & 0xFFFFL) << 16;
                        note = "  (LUI -> base 0x" + Long.toHexString(base) + ")";
                        if (!(base >= TBL_LO && base < TBL_HI)) {
                            boolean ok = false;
                            for (long e : EXTRA) if (base == e) ok = true;
                            if (!ok) continue;
                        }
                    }

                    String line = "  " + ins.getAddress() + "  " + ins.toString() + note;
                    log(line);
                    Function f = getFunctionContaining(ins.getAddress());
                    if (f != null) {
                        funcs.add(f);
                        evidence.computeIfAbsent(f, k -> new ArrayList<>()).add(line.trim());
                    }
                    n++;
                }
            }
        }

        log("");
        log("=== totals ===");
        log("  matching instructions: " + n);
        log("  distinct functions   : " + funcs.size());
        log("");

        log("=== functions, by number of id-table references ===");
        List<Map.Entry<Function, List<String>>> sorted = new ArrayList<>(evidence.entrySet());
        sorted.sort((a, b) -> Integer.compare(b.getValue().size(), a.getValue().size()));
        for (Map.Entry<Function, List<String>> e : sorted) {
            log("  " + e.getKey().getName() + " @ " + e.getKey().getEntryPoint()
                + "   " + e.getValue().size() + " ref(s)");
            for (String s : e.getValue()) log("      " + s);
        }
        log("");

        // decompile the top candidates
        log("=== decompilation of the top candidates ===");
        int shown = 0;
        for (Map.Entry<Function, List<String>> e : sorted) {
            if (shown >= 10) { log("  ... (more omitted)"); break; }
            Function f = e.getKey();
            log("");
            log("################ " + f.getName() + " @ " + f.getEntryPoint()
                + "  body=" + f.getBody().getNumAddresses() + " bytes ################");
            try {
                DecompileResults dr = dec.decompileFunction(f, 60, monitor);
                if (dr != null && dr.decompileCompleted()) {
                    String c = dr.getDecompiledFunction().getC();
                    log(c.length() > 3500 ? c.substring(0, 3500) + "\n... [truncated]" : c);
                } else {
                    log("  (decompilation incomplete)");
                }
            } catch (Exception ex) {
                log("  (decompile error: " + ex.getMessage() + ")");
            }
            shown++;
        }

        String path = System.getProperty("user.dir") + File.separator + "tagteam-idtable-out.txt";
        try (PrintWriter pw = new PrintWriter(new FileWriter(path))) {
            pw.print(out);
        }
        println("TagTeamIdTable.java> wrote " + out.length() + " chars to tagteam-idtable-out.txt");
    }
}
