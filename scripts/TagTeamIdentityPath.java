// TagTeamIdentityPath.java — the high-signal intersection search for character identity.
//
// WHY (corrected search shape): an earlier exact-address search for the name-id table returned 0
// and I wrongly concluded "unreferenced". The table is in fact reached by MIPS LUI, whose Ghidra
// scalar is the HIGH 16 BITS only (lui $v1,0x8A7). Bucketing by 4K page showed 0x08A70000 is
// referenced 1815 times and 0x08A80000 3468 times -- two of the most-used data pages in the binary.
//
// This script finds functions that BOTH:
//   1. reference the 0x08A7xxxx / 0x08A8xxxx pages (via LUI high-half OR a full scalar), and
//   2. CALL the message resolver FUN_0883ee74 (which turns a message ID into text).
// That intersection is where a fighter slot's name id is chosen. It is a small, high-signal set.

import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.scalar.Scalar;
import ghidra.program.model.symbol.*;

import java.io.*;
import java.util.*;

public class TagTeamIdentityPath extends GhidraScript {

    static final long RESOLVER = 0x0883EE74L;
    static final long PAGE_A7_LO = 0x08A70000L, PAGE_A7_HI = 0x08A80000L;
    static final long PAGE_A8_LO = 0x08A80000L, PAGE_A8_HI = 0x08A90000L;

    StringBuilder out = new StringBuilder();
    DecompInterface dec = new DecompInterface();
    void log(String s) { out.append(s).append('\n'); }

    // does this instruction reference the target data pages?
    // handles BOTH a folded full address and a bare LUI high-half.
    boolean refsPages(Instruction ins) {
        for (int op = 0; op < ins.getNumOperands(); op++) {
            for (Object o : ins.getOpObjects(op)) {
                long v;
                if (o instanceof Scalar) v = ((Scalar) o).getUnsignedValue();
                else if (o instanceof Address) v = ((Address) o).getOffset();
                else continue;

                if (v >= PAGE_A7_LO && v < PAGE_A8_HI) return true;

                // LUI: scalar is the high 16 bits -> reconstruct and test
                if (ins.getMnemonicString().equalsIgnoreCase("lui")) {
                    long hi = (v & 0xFFFFL) << 16;
                    if (hi >= PAGE_A7_LO && hi < PAGE_A8_HI) return true;
                }
            }
        }
        return false;
    }

    boolean callsResolver(Instruction ins) {
        if (ins.getMnemonicString().startsWith("jal") || ins.getFlowType().isCall()) {
            for (Object o : ins.getOpObjects(0)) {
                long v;
                if (o instanceof Scalar) v = ((Scalar) o).getUnsignedValue();
                else if (o instanceof Address) v = ((Address) o).getOffset();
                else continue;
                if ((v & 0xFFFFFFFFL) == RESOLVER) return true;
            }
        }
        return false;
    }

    @Override
    public void run() throws Exception {
        dec.openProgram(currentProgram);

        log("=== TagTeamIdentityPath ===");
        log("resolver        : 0x" + Long.toHexString(RESOLVER));
        log("id-table pages  : 0x" + Long.toHexString(PAGE_A7_LO) + " - 0x" + Long.toHexString(PAGE_A8_HI));
        log("criterion       : references those pages AND calls the resolver");
        log("");

        // pass: walk each function's instructions, track both conditions
        Map<Function, int[]> hits = new LinkedHashMap<>();   // fn -> [pageRefs, resolverCalls]
        Map<Function, String> samples = new LinkedHashMap<>();

        FunctionManager fm = currentProgram.getFunctionManager();
        for (Function f : fm.getFunctions(true)) {
            if (monitor.isCancelled()) break;
            int pageRefs = 0, resCalls = 0;
            String sample = "";
            for (Instruction ins : currentProgram.getListing().getInstructions(f.getBody(), true)) {
                if (refsPages(ins)) {
                    pageRefs++;
                    if (sample.isEmpty()) sample = ins.getAddress() + "  " + ins.toString();
                }
                if (callsResolver(ins)) resCalls++;
            }
            if (pageRefs > 0 && resCalls > 0) {
                hits.put(f, new int[]{pageRefs, resCalls});
                samples.put(f, sample);
            }
        }

        log("=== INTERSECTION: " + hits.size() + " function(s) ===");
        List<Map.Entry<Function, int[]>> sorted = new ArrayList<>(hits.entrySet());
        sorted.sort((a, b) -> Integer.compare(b.getValue()[1], a.getValue()[1]));  // most resolver calls first
        for (Map.Entry<Function, int[]> e : sorted) {
            log(String.format("  %s @ %s   pageRefs=%d  resolverCalls=%d   e.g. %s",
                e.getKey().getName(), e.getKey().getEntryPoint(),
                e.getValue()[0], e.getValue()[1], samples.get(e.getKey())));
        }
        log("");

        log("=== decompilation of the top candidates ===");
        int shown = 0;
        for (Map.Entry<Function, int[]> e : sorted) {
            if (shown >= 6) { log("  ... (more omitted)"); break; }
            Function f = e.getKey();
            log("");
            log("################ " + f.getName() + " @ " + f.getEntryPoint()
                + "  body=" + f.getBody().getNumAddresses() + " bytes"
                + "  pageRefs=" + e.getValue()[0] + " resolverCalls=" + e.getValue()[1] + " ################");
            try {
                DecompileResults dr = dec.decompileFunction(f, 90, monitor);
                if (dr != null && dr.decompileCompleted()) {
                    String c = dr.getDecompiledFunction().getC();
                    log(c.length() > 5000 ? c.substring(0, 5000) + "\n... [truncated]" : c);
                } else {
                    log("  (decompilation incomplete)");
                }
            } catch (Exception ex) {
                log("  (decompile error: " + ex.getMessage() + ")");
            }
            shown++;
        }

        String path = System.getProperty("user.dir") + File.separator + "tagteam-identitypath-out.txt";
        try (PrintWriter pw = new PrintWriter(new FileWriter(path))) { pw.print(out); }
        println("TagTeamIdentityPath.java> wrote " + out.length() + " chars to tagteam-identitypath-out.txt");
    }
}
