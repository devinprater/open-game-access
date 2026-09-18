// TagTeamDataRefs.java — which data addresses does the code reference AS LITERALS at all?
//
// WHY: a search for literal references to the name-id table region (0x08A75000-0x08A76000) found
// ZERO instructions. That is either (a) the table is reached via a computed base, or (b) my search
// is not seeing the addressing form. Both must be distinguished, because guessing wrong here sends
// the whole investigation the wrong way.
//
// This script maps the code's ENTIRE literal data-reference profile: for every instruction operand
// that points into the load segment, bucket it by 4K page. The result shows which data areas the
// code actually names, and whether 0x08A7xxxx is referenced at all.
//
// MIPS specifics handled: 32-bit constants are built as LUI(hi) + ORI/ADDIU(lo). Ghidra usually
// folds these, but when it does not the LUI scalar alone is the high half -- so this script also
// records LUI hi-halves so a base can be recognised either way.

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.scalar.Scalar;

import java.io.*;
import java.util.*;

public class TagTeamDataRefs extends GhidraScript {

    static final long SEG_LO = 0x08804040L;   // from the ELF program headers
    static final long SEG_HI = 0x08A8149DL;   // 0x08804040 + 0x27D45D

    StringBuilder out = new StringBuilder();
    void log(String s) { out.append(s).append('\n'); }

    @Override
    public void run() throws Exception {
        log("=== TagTeamDataRefs: the code's literal data-reference profile ===");
        log("load segment: 0x" + Long.toHexString(SEG_LO) + " - 0x" + Long.toHexString(SEG_HI));
        log("");

        Map<Long, Integer> pageHits = new TreeMap<>();          // 4K page -> count
        Map<Long, String> pageSample = new TreeMap<>();
        int total = 0, luiOnly = 0;

        Listing lst = currentProgram.getListing();
        InstructionIterator it = lst.getInstructions(true);
        while (it.hasNext()) {
            Instruction ins = it.next();
            String mnem = ins.getMnemonicString();
            for (int op = 0; op < ins.getNumOperands(); op++) {
                Object[] objs = ins.getOpObjects(op);
                for (Object o : objs) {
                    if (!(o instanceof Scalar)) continue;
                    long v = ((Scalar) o).getUnsignedValue();

                    long target = -1;
                    if (v >= SEG_LO && v <= SEG_HI) {
                        target = v;
                    } else if (mnem.equalsIgnoreCase("lui")) {
                        long hi = (v & 0xFFFFL) << 16;
                        if (hi >= SEG_LO && hi <= SEG_HI) { target = hi; luiOnly++; }
                    }
                    if (target < 0) continue;

                    long page = (target >> 12) << 12;   // 4K page
                    pageHits.merge(page, 1, Integer::sum);
                    pageSample.putIfAbsent(page, ins.getAddress() + "  " + ins.toString());
                    total++;
                }
            }
        }

        log("=== total literal data references: " + total + "  (" + luiOnly + " from LUI high-halves) ===");
        log("");
        log("=== 4K pages referenced, by count (descending) ===");
        List<Map.Entry<Long, Integer>> sorted = new ArrayList<>(pageHits.entrySet());
        sorted.sort((a, b) -> Integer.compare(b.getValue(), a.getValue()));
        for (Map.Entry<Long, Integer> e : sorted) {
            log(String.format("  0x%08X  %5d ref(s)   e.g. %s", e.getKey(), e.getValue(),
                pageSample.get(e.getKey())));
        }

        log("");
        log("=== ANSWER: is 0x08A70000-0x08A7FFFF referenced at all? ===");
        long want = 0x08A70000L;
        boolean any = false;
        for (Long p : pageHits.keySet()) {
            if (p >= 0x08A70000L && p < 0x08A80000L) {
                log(String.format("  YES: page 0x%08X  %d ref(s)", p, pageHits.get(p)));
                any = true;
            }
        }
        if (!any) log("  NO -- the 0x08A7xxxx region is NOT referenced by any literal in the code.");

        String path = System.getProperty("user.dir") + File.separator + "tagteam-datarefs-out.txt";
        try (PrintWriter pw = new PrintWriter(new FileWriter(path))) {
            pw.print(out);
        }
        println("TagTeamDataRefs.java> wrote " + out.length() + " chars to tagteam-datarefs-out.txt");
    }
}
