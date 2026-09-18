// DisFindLoaders.java -- decompile the code that references Dissidia's resource-name strings.
//
// !!! SUPERSEDED -- ITS TARGETS USE THE WRONG ADDRESS SPACE, SO IT REPORTS 0 REFERENCES !!!
//
//   The TARGETS below are RAM-space addresses (0x08B76E94 ...). The DISSIDIA_ELF project was imported
//   with ElfLoader, so its listing uses ELF VADDR space (0x0037...). They differ by the load base
//   0x08804000 (confirmed in doc section 19):
//
//       RAM-space target 0x08B76E94  ->  equivalent vaddr 0x00372E94
//
//   So getReferencesTo() finds nothing in either project, and "0 references / 0 distinct functions"
//   is an ARTIFACT of the address space, not evidence the code never reads those strings.
//
//   It also relied on Ghidra's reference table, which a raw BinaryLoader import does not populate --
//   a second reason for the same null.
//
// WHAT TO USE INSTEAD
//   DisFindStrings.java -- searches Ghidra's memory for the string BYTES (no precomputed address to
//   get wrong) and decompiles each referrer. That run found 23 occurrences / 10 functions
//   (doc section 15).
//
// Kept as a record of the bug. Do not run it expecting results.
//
// WHY THIS SCRIPT EXISTED
//   A sibling accessibility mod (FFVIII) captures dialog by hooking the point where the engine hands
//   DECODED text to the renderer, instead of searching game data for text. That is the generalisable
//   lesson: follow the CODE that produces text, not the bytes that might contain it.
//
//   Dissidia's EBOOT.dec is a plain string table near file offset 0x371000 containing its own
//   resource names. The project was imported with a raw BinaryLoader at base 0x08804000, so the
//   Ghidra address of a string is exactly  base + file_offset. Those addresses are precomputed below.
//
//   This script resolves every reference TO those strings and decompiles each referencing function,
//   so the loader/decoder can be read directly.
//
// USAGE (headless, via the project's run-analysis.bat pattern):
//   analyzeHeadless <projDir> DISSIDIA -process EBOOT.dec -noanalysis
//       -scriptPath "<projDir>" -postScript DisFindLoaders.java
//
// @category OGA

import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;

import java.io.PrintWriter;
import java.io.File;
import java.util.*;

public class DisFindLoaders extends GhidraScript {

    /** (address, label) pairs: base 0x08804000 + file offset of each string. */
    private static final Object[][] TARGETS = {
        { 0x08B76E94L, "pause_help.bin" },
        { 0x08B837F8L, "pause_help.bin (2nd)" },
        { 0x08B8C8C8L, "pause_help.bin (3rd)" },
        { 0x08B7C958L, "item_help" },
        { 0x08B7C754L, "accessory_help" },
        { 0x08B76FD9L, "name.bin" },
        { 0x08B7C7CCL, "system.bin" },
        { 0x08B76F08L, "battle_result.bin" },
        { 0x08B75FDCL, "SYSTEM_FONT::Draw" },
        { 0x08B75FBCL, "libfont.prx" },
        { 0x08B760D4L, "VOLATILE_MEMORY_LOADER" },
    };

    private static final int MAX_FUNCS_PER_STRING = 6;
    private static final int MAX_DECOMPILE_CHARS = 7000;

    @Override
    public void run() throws Exception {
        String outPath = System.getProperty("user.home")
                + "/oga-ghidra-dissidia/loaders-report.txt";
        PrintWriter out = new PrintWriter(new File(outPath), "UTF-8");

        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);

        Set<Long> seen = new HashSet<>();
        int totalRefs = 0;
        int totalFuncs = 0;

        for (Object[] t : TARGETS) {
            long addr = (Long) t[0];
            String label = (String) t[1];
            Address a = toAddr(addr);

            out.println("################################################################");
            out.println("### \"" + label + "\"  at " + a);
            out.println("################################################################");

            ReferenceIterator refs = currentProgram.getReferenceManager().getReferencesTo(a);
            int n = 0;
            int used = 0;
            while (refs.hasNext()) {
                Reference r = refs.next();
                n++;
                totalRefs++;
                Address from = r.getFromAddress();
                Function f = getFunctionContaining(from);
                out.println("  ref from " + from
                            + "  type=" + r.getReferenceType()
                            + "  func=" + (f == null ? "(none)" : f.getName() + " @ " + f.getEntryPoint()));

                if (f != null && used < MAX_FUNCS_PER_STRING) {
                    long key = f.getEntryPoint().getOffset();
                    if (seen.add(key)) {
                        used++;
                        totalFuncs++;
                        out.println();
                        out.println("  ---- " + f.getName() + " @ " + f.getEntryPoint() + " ----");
                        try {
                            DecompileResults dr = di.decompileFunction(f, 60, monitor);
                            if (dr != null && dr.decompileCompleted()) {
                                String txt = dr.getDecompiledFunction().getC();
                                if (txt.length() > MAX_DECOMPILE_CHARS) {
                                    txt = txt.substring(0, MAX_DECOMPILE_CHARS) + "\n/* ...truncated... */";
                                }
                                out.println(txt);
                            } else {
                                out.println("  (decompile did not complete)");
                            }
                        } catch (Exception e) {
                            out.println("  (decompile threw: " + e + ")");
                        }
                        out.println("  ---- end ----");
                    }
                }
                out.println();
            }
            if (n == 0) {
                out.println("  (no references -- may be reached only via a computed pointer)");
            }
            out.println();
        }

        out.println("### SUMMARY: " + totalRefs + " references, "
                    + totalFuncs + " distinct functions decompiled");
        out.flush();
        out.close();
        di.dispose();

        println("DisFindLoaders -> " + outPath);
    }
}
