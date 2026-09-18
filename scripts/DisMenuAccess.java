// DisMenuAccess.java -- find who creates and reads the menu manager, to locate the selection field.
//
// WHERE THIS STANDS
//   FUN_002489d0 is the menu manager constructor. Section 16 found it sets param_1[8] = -1 -- a
//   selection sentinel -- then builds five parallel subsystems. Sections 17/20 corrected an earlier
//   error: DAT_00397770 is a CHAPTER-NAME TABLE, not the manager; the real manager is reached via
//   FUN_00103c60(DAT_00392cd8) -> *(u32*)(DAT_00392cd8 + 0x2000), which read live as 0x08C5DC80.
//
//   The remaining question is which field holds the SELECTION. param_1 is a runtime pointer, so the
//   only way in is the code: find who CALLS the constructor (that caller holds the object) and who
//   READS the singleton key. Both are plain reference queries, bounded and decisive.
//
// USAGE
//   analyzeHeadless <projDir> DISSIDIA_ELF -process EBOOT.dec -noanalysis
//       -scriptPath "<projDir>" -postScript DisMenuAccess.java
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

public class DisMenuAccess extends GhidraScript {

    /** The menu manager constructor (section 16). */
    private static final long CONSTRUCTOR = 0x002489d0L;

    /** The singleton key: FUN_00103c60(p) returns *(u32*)(p + 0x2000). */
    private static final long SINGLETON_KEY = 0x00392cd8L;

    /** The dispatcher, whose param_1 IS the manager (section 17/20). */
    private static final long DISPATCHER = 0x0024932cL;

    private static final int MAX_CHARS = 14000;

    @Override
    public void run() throws Exception {
        String outPath = System.getProperty("user.home")
                + "/oga-ghidra-dissidia/menuaccess-report.txt";
        PrintWriter out = new PrintWriter(new File(outPath), "UTF-8");

        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);

        // ---- 1. who calls the constructor
        section(out, di, "CALLERS OF THE MENU MANAGER CONSTRUCTOR",
                new long[] { CONSTRUCTOR }, true);

        // ---- 2. who reads/writes the singleton key
        section(out, di, "REFERENCES TO THE SINGLETON KEY (DAT_00392cd8)",
                new long[] { SINGLETON_KEY }, false);

        // ---- 3. the dispatcher's own callers (the layer above)
        section(out, di, "CALLERS OF THE DISPATCHER (FUN_0024932c)",
                new long[] { DISPATCHER }, true);

        out.flush();
        out.close();
        di.dispose();
        println("DisMenuAccess -> " + outPath);
    }

    private void section(PrintWriter out, DecompInterface di, String title,
                         long[] addrs, boolean followCallers) throws Exception {
        out.println("################################################################");
        out.println("### " + title);
        out.println("################################################################");

        for (long addr : addrs) {
            Address a = toAddr(addr);
            out.println("--- target " + a + " ---");

            ReferenceIterator refs = currentProgram.getReferenceManager().getReferencesTo(a);
            List<Long> froms = new ArrayList<>();
            while (refs.hasNext()) {
                Reference r = refs.next();
                froms.add(r.getFromAddress().getOffset());
            }
            out.println("  " + froms.size() + " reference(s)");

            Set<Long> done = new HashSet<>();
            int shown = 0;
            for (Long from : froms) {
                Address fa = toAddr(from);
                Function f = getFunctionContaining(fa);
                out.println("    from " + fa + (f == null ? "" : "   in " + f.getName() + " @ " + f.getEntryPoint()));
                if (f == null) continue;
                long key = f.getEntryPoint().getOffset();
                if (!done.add(key)) continue;
                if (shown >= 6) continue;
                shown++;
                out.println();
                out.println("    ==== " + f.getName() + " @ " + f.getEntryPoint()
                            + "  size=" + f.getBody().getNumAddresses() + " ====");
                try {
                    DecompileResults dr = di.decompileFunction(f, 150, monitor);
                    if (dr != null && dr.decompileCompleted()) {
                        String txt = dr.getDecompiledFunction().getC();
                        if (txt.length() > MAX_CHARS) txt = txt.substring(0, MAX_CHARS) + "\n/* ...truncated... */";
                        out.println(txt);
                    } else {
                        out.println("    (decompile did not complete)");
                    }
                } catch (Exception e) {
                    out.println("    (decompile threw: " + e + ")");
                }
                out.println("    ==== end ====");
                out.println();
            }
            out.println();
        }
    }
}
