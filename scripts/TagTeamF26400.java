// TagTeamF26400.java — full decompilation plus the id-table values it indexes.
//
// WHY: FUN_08a26400 is the strongest identity candidate found so far. It BOTH references the
// message-id tables (0x08A75538 / 0x08A7553C) AND calls the text resolver FUN_0883ee74 five times,
// with arguments derived from those tables and from object fields (+0x80, +0x84, +0x1ec).
//
// Verified facts this relies on (see docs/reverse-engineering/dbz-tenkaichi-tag-team.md):
//   - names are NOT stored as string pointers; the game stores MESSAGE IDS and resolves them via
//     FUN_0883ee74(message_id, -1)
//   - the id tables are STATIC DATA in EBOOT.dec:
//         0x08A75538:  505 506 507 508 509 510 171 172 169 170 173 174 162 163 ...
//         0x08A7553C:  506 507 508 509 510 171 172 169 170 173 174 162 163 ...   (one word later)
//   - an earlier "+0x1A1 field" claim was a p-code SPACE ID misread and is retracted
//
// This script prints:
//   1. the full decompilation of FUN_08a26400
//   2. every FUN_0883ee74 call site inside it, with the argument expression
//   3. the id-table contents around the offsets it indexes, straight from the ELF bytes
//
// Output goes to tagteam-f26400-out.txt (a written artefact, not just stdout).

import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.pcode.*;
import ghidra.program.model.scalar.Scalar;

import java.io.*;
import java.util.*;

public class TagTeamF26400 extends GhidraScript {

    static final long TARGET = 0x08A26400L;
    static final long RESOLVER = 0x0883EE74L;
    static final long TBL_A = 0x08A75538L;
    static final long TBL_B = 0x08A7553CL;
    static final int  TBL_N = 48;

    StringBuilder out = new StringBuilder();
    DecompInterface dec = new DecompInterface();
    void log(String s) { out.append(s).append('\n'); }

    void dumpTable(long base, int n) {
        log("  " + String.format("0x%08X:", base));
        StringBuilder row = new StringBuilder("    ");
        for (int i = 0; i < n; i++) {
            long a = base + i * 4L;
            try {
                int v = getInt(toAddr(a));
                row.append(String.format("%5d ", v));
            } catch (Exception e) {
                row.append("  ??  ");
            }
            if ((i + 1) % 12 == 0) { log(row.toString()); row = new StringBuilder("    "); }
        }
        if (row.length() > 4) log(row.toString());
    }

    @Override
    public void run() throws Exception {
        dec.openProgram(currentProgram);
        Function f = getFunctionAt(toAddr(TARGET));
        if (f == null) { printerr("no function at 0x" + Long.toHexString(TARGET)); return; }

        log("=== TagTeamF26400 ===");
        log("target   : " + f.getName() + " @ " + f.getEntryPoint() + "  body=" + f.getBody().getNumAddresses() + " bytes");
        log("resolver : 0x" + Long.toHexString(RESOLVER));
        log("");

        // ---- 1. full decompilation ----
        log("=== FULL DECOMPILATION ===");
        DecompileResults dr = dec.decompileFunction(f, 120, monitor);
        if (dr != null && dr.decompileCompleted()) {
            log(dr.getDecompiledFunction().getC());
        } else {
            log("  (decompilation incomplete)");
        }
        log("");

        // ---- 2. resolver call sites with argument slices, via p-code ----
        log("=== FUN_0883ee74 CALL SITES INSIDE THIS FUNCTION (p-code) ===");
        if (dr != null && dr.getHighFunction() != null) {
            HighFunction hf = dr.getHighFunction();
            Iterator<PcodeOpAST> ops = hf.getPcodeOps();
            int hits = 0;
            while (ops.hasNext()) {
                PcodeOpAST op = ops.next();
                if (op.getOpcode() != PcodeOp.CALL || op.getNumInputs() < 2) continue;
                Varnode dest = op.getInput(0);
                if (!dest.isAddress() || !dest.getAddress().equals(toAddr(RESOLVER))) continue;
                hits++;
                log("  callsite " + op.getSeqnum().getTarget());
                log("    arg0(key) : " + op.getInput(1));
                PcodeOp def = op.getInput(1).getDef();
                if (def != null) log("      def     : " + def.toString());
                if (op.getNumInputs() > 2) log("    arg1      : " + op.getInput(2));
            }
            log("  total resolver calls: " + hits);
        } else {
            log("  (no high function available)");
        }
        log("");

        // ---- 3. the id tables it indexes ----
        log("=== ID TABLE CONTENTS (from the ELF, static data) ===");
        log("  Table A (indexed as tableA[i]):");
        dumpTable(TBL_A, TBL_N);
        log("");
        log("  Table B (indexed as tableB[i]):");
        dumpTable(TBL_B, TBL_N);
        log("");

        // ---- 4. what sits just after, in case the tables are {id, ptr} or {id, count} pairs ----
        log("=== raw words at 0x08A75538 .. 0x08A755B4 (as hex, to spot structure) ===");
        for (int i = 0; i < 32; i++) {
            long a = TBL_A + i * 4L;
            try {
                int v = getInt(toAddr(a));
                log(String.format("  0x%08X  %08X  %10d", a, v, v));
            } catch (Exception e) { log(String.format("  0x%08X  (unreadable)", a)); }
        }

        String path = System.getProperty("user.dir") + File.separator + "tagteam-f26400-out.txt";
        try (PrintWriter pw = new PrintWriter(new FileWriter(path))) { pw.print(out); }
        println("TagTeamF26400.java> wrote " + out.length() + " chars to tagteam-f26400-out.txt");
    }
}
