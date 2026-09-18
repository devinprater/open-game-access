// DisFindStrings.java -- locate Dissidia's own name strings IN MEMORY and decompile their referrers.
//
// WHY THIS VERSION, NOT THE EARLIER ONE
//   DisFindLoaders.java used HARD-CODED addresses computed as base + file_offset, which was wrong:
//   the project is imported with ElfLoader, so the listing uses base + ELF vaddr (the first LOAD
//   segment is file_off 0x74 / vaddr 0, a 0x74 skew). That scan reported 0 references across 683,649
//   instructions -- a silent, convincing wrong answer.
//
//   This version does not trust precomputed addresses at all: it SEARCHES Ghidra's memory for the
//   string bytes, so the address it uses is by construction the one in the listing. Then it resolves
//   references and decompiles the referring functions.
//
// WHY THESE STRINGS
//   The engine keeps a named-function registry in RAM (MENU MANAGER, MENU_MANAGER::ExecuteUpdate,
//   SYSTEM_FONT::Draw, VOLATILE_MEMORY_LOADER). No u32 in RAM points at those name strings, so the
//   lookup is by name at runtime. The code that COMPARES those names is therefore the only way in,
//   and this script is what finds it.
//
// USAGE
//   analyzeHeadless <projDir> DISSIDIA_ELF -process EBOOT.dec -noanalysis
//       -scriptPath "<projDir>" -postScript DisFindStrings.java
//
// @category OGA

import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;

import java.io.PrintWriter;
import java.io.File;
import java.util.*;

public class DisFindStrings extends GhidraScript {

    private static final String[] TARGETS = {
        "MENU_MANAGER::ExecuteUpdate",
        "MENU MANAGER",
        "SYSTEM_FONT::Draw",
        "SYSTEM_FONT::Update",
        "VOLATILE_MEMORY_LOADER",
        "pause_help.bin",
        "item_help.bin",
        "accessory_help.bin",
        "name.bin",
        "system.bin",
        "general_archive",
        "menu_lang",
    };

    private static final int MAX_DECOMPILE_CHARS = 9000;
    private static final int MAX_FUNCS_PER_HIT = 4;

    @Override
    public void run() throws Exception {
        String outPath = System.getProperty("user.home")
                + "/oga-ghidra-dissidia/strings-report.txt";
        PrintWriter out = new PrintWriter(new File(outPath), "UTF-8");

        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);

        int totalHits = 0;
        int totalFuncs = 0;
        Set<Long> seenFuncs = new HashSet<>();

        for (String target : TARGETS) {
            byte[] needle = target.getBytes("US-ASCII");
            List<Address> hits = new ArrayList<>();

            Memory mem = currentProgram.getMemory();
            for (MemoryBlock b : mem.getBlocks()) {
                if (!b.isInitialized()) continue;
                try {
                    // Memory.findBytes(start, end, bytes, masks, forward, monitor) -- the Memory
                    // interface has this, MemoryBlock does NOT. (Caught by compiling with javac
                    // first; Ghidra would have reported only ClassNotFoundException.)
                    Address cur = b.getStart();
                    while (cur != null && cur.compareTo(b.getEnd()) < 0 && hits.size() < 8) {
                        Address found = mem.findBytes(cur, b.getEnd(), needle, null, true, monitor);
                        if (found == null) break;
                        hits.add(found);
                        cur = found.add(1);
                    }
                } catch (Exception e) {
                    // ignore a block we cannot scan
                }
            }

            out.println("################################################################");
            out.println("### \"" + target + "\"  -- " + hits.size() + " occurrence(s) in the listing");
            out.println("################################################################");

            if (hits.isEmpty()) {
                out.println("  (string not present in the imported program)");
                out.println();
                continue;
            }

            for (Address a : hits) {
                totalHits++;
                out.println("  --- string at " + a + " ---");
                ReferenceIterator refs = currentProgram.getReferenceManager().getReferencesTo(a);
                int n = 0;
                int used = 0;
                while (refs.hasNext()) {
                    Reference r = refs.next();
                    n++;
                    Address from = r.getFromAddress();
                    Function f = getFunctionContaining(from);
                    out.println("       ref from " + from + " type=" + r.getReferenceType()
                                + " func=" + (f == null ? "(none)" : f.getName() + " @ " + f.getEntryPoint()));
                    if (f != null && used < MAX_FUNCS_PER_HIT && seenFuncs.add(f.getEntryPoint().getOffset())) {
                        used++;
                        totalFuncs++;
                        out.println();
                        out.println("       ==== " + f.getName() + " @ " + f.getEntryPoint() + " ====");
                        try {
                            DecompileResults dr = di.decompileFunction(f, 90, monitor);
                            if (dr != null && dr.decompileCompleted()) {
                                String txt = dr.getDecompiledFunction().getC();
                                if (txt.length() > MAX_DECOMPILE_CHARS) {
                                    txt = txt.substring(0, MAX_DECOMPILE_CHARS) + "\n/* ...truncated... */";
                                }
                                out.println(txt);
                            } else {
                                out.println("       (decompile did not complete)");
                            }
                        } catch (Exception e) {
                            out.println("       (decompile threw: " + e + ")");
                        }
                        out.println("       ==== end ====");
                    }
                }
                if (n == 0) {
                    out.println("       (no references -- reached by computed pointer or by name compare)");
                }
                out.println();
            }
            out.println();
        }

        out.println("### SUMMARY: " + totalHits + " string occurrences, "
                    + totalFuncs + " distinct functions decompiled");
        out.flush();
        out.close();
        di.dispose();
        println("DisFindStrings -> " + outPath + " (" + totalHits + " hits, " + totalFuncs + " funcs)");
    }
}
