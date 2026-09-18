// DisMenuInit.java -- decompile FUN_0024adf8, the call that runs on the menu object right after
// the constructor, plus the sibling menu functions around it.
//
// WHERE THIS STANDS (doc sections 16-21)
//   FUN_002dbf50 is the boot function. Around menu setup it does:
//       FUN_002489d0(DAT_00397770);              <- menu manager constructor (param_1 == DAT_00397770)
//       ...
//       FUN_0024adf8(DAT_00397770, auStack_30);  <- object + context, AFTER construction
//   The selection sentinel is param_1[8] = -1 set by the constructor, and the remaining question is
//   which field holds the live selection. FUN_0024adf8 is the next link, so read it and the nearby
//   0x0024xxxx family that the boot sequence touches.
//
// USAGE
//   analyzeHeadless <projDir> DISSIDIA_ELF -process EBOOT.dec -noanalysis
//       -scriptPath "<projDir>" -postScript DisMenuInit.java
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

public class DisMenuInit extends GhidraScript {

    private static final long[] FUNCS = {
        0x0024adf8L,   // menu object + context, called after construction  <-- primary target
        0x00246d64L,   // called just after, via FUN_00246a78(uVar7, auStack_2c)
        0x00246a78L,
        0x00269758L,   // FUN_00269758(DAT_00398358, auStack_2c), right after the constructor
    };

    private static final int MAX_CHARS = 16000;

    @Override
    public void run() throws Exception {
        String outPath = System.getProperty("user.home")
                + "/oga-ghidra-dissidia/menuinit-report.txt";
        PrintWriter out = new PrintWriter(new File(outPath), "UTF-8");

        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);

        for (long addr : FUNCS) {
            Address a = toAddr(addr);
            out.println("################################################################");
            out.println("### FUNCTION " + a);
            out.println("################################################################");

            Function f = getFunctionAt(a);
            if (f == null) f = getFunctionContaining(a);
            if (f == null) {
                out.println("  (no function at/containing this address)");
                out.println();
                continue;
            }
            out.println("  " + f.getName() + " @ " + f.getEntryPoint()
                        + "  size=" + f.getBody().getNumAddresses());

            // callers, to know where it sits in the menu lifecycle
            List<Long> froms = new ArrayList<>();
            ReferenceIterator refs = currentProgram.getReferenceManager().getReferencesTo(f.getEntryPoint());
            while (refs.hasNext()) {
                Reference r = refs.next();
                froms.add(r.getFromAddress().getOffset());
            }
            out.println("  callers (" + froms.size() + "):");
            Set<Long> seen = new HashSet<>();
            for (Long from : froms) {
                Function cf = getFunctionContaining(toAddr(from));
                String nm = cf == null ? "" : "  in " + cf.getName() + " @ " + cf.getEntryPoint();
                if (seen.add(from)) out.println("      " + toAddr(from) + nm);
            }
            out.println();

            try {
                DecompileResults dr = di.decompileFunction(f, 180, monitor);
                if (dr != null && dr.decompileCompleted()) {
                    String txt = dr.getDecompiledFunction().getC();
                    if (txt.length() > MAX_CHARS) txt = txt.substring(0, MAX_CHARS) + "\n/* ...truncated... */";
                    out.println(txt);
                } else {
                    out.println("  (decompile did not complete)");
                }
            } catch (Exception e) {
                out.println("  (decompile threw: " + e + ")");
            }
            out.println();
        }

        out.flush();
        out.close();
        di.dispose();
        println("DisMenuInit -> " + outPath);
    }
}
