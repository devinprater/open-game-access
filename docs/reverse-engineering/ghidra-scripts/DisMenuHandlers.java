// DisMenuHandlers.java -- decompile the menu manager's registered EVENT HANDLERS.
//
// WHY THESE
//   decompile2 showed FUN_002489d0 (the "MENU MANAGER" constructor) END by registering three
//   handlers on the object returned by FUN_00103c60(DAT_00392cd8):
//       FUN_00103db0(mgr, 1,          0x88ba,        FUN_0036926c)
//       FUN_00103db0(mgr, 0xffffffff, &DAT_00011183, FUN_00369290)
//       FUN_00103db0(mgr, 0xffffffff, &DAT_00011189, FUN_003692b4)
//
//   Doc sections 24/25 concluded that the cursor hunt has exhausted the MANAGER layer and needs the
//   menu SCREEN code instead. These three handlers are registered BY the manager and are the only
//   entry points the engine will call into menu logic, so they are that layer.
//
//   FUN_00103db0 is included so the handler SIGNATURE (what the engine passes) is legible, and
//   FUN_00103c60 because it produces the object the handlers hang off.
//
// WHAT WOULD SETTLE THE CURSOR QUESTION
//   A field read to CHOOSE an item, or incremented/decremented per up/down, or compared to a count.
//   It is NOT *(node+0x17) (a debounce timer, re-confirmed in decompile3) and NOT DAT_00392cd8+0x20
//   (a constant 290 that never moved across presses on two different menus).
//
// USAGE
//   analyzeHeadless <projDir> DISSIDIA_ELF -process EBOOT.dec -noanalysis
//       -scriptPath "<projDir>" -postScript DisMenuHandlers.java
//
// @category OGA

import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;

import java.io.File;
import java.io.PrintWriter;

public class DisMenuHandlers extends GhidraScript {

    private static final long[] FUNCS = {
        0x0036926cL,   // registered handler #1 (event id 1)
        0x00369290L,   // registered handler #2
        0x003692b4L,   // registered handler #3
        0x00103db0L,   // the REGISTRATION fn -- reveals the handler signature
        0x00103c60L,   // produces the object the handlers are registered on
        0x0024aee0L,   // shared callee of FUN_00248dec / FUN_00248ea8
    };

    private static final int MAX_CHARS = 22000;

    @Override
    public void run() throws Exception {
        String outPath = System.getProperty("user.home")
                + "/oga-ghidra-dissidia/menu-handlers-report.txt";
        PrintWriter out = new PrintWriter(new File(outPath), "UTF-8");

        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);

        for (long addr : FUNCS) {
            Address a = toAddr(addr);
            out.println("################################################################");
            out.println("### FUNCTION " + Long.toHexString(addr));
            out.println("################################################################");

            Function f = getFunctionAt(a);
            if (f == null) {
                f = getFunctionContaining(a);
            }
            if (f == null) {
                out.println("  (no function at/containing " + a + ")");
                out.println();
                continue;
            }
            out.println("  " + f.getName() + " @ " + f.getEntryPoint()
                        + "  size=" + f.getBody().getNumAddresses());

            out.println("  -- call sites / references to this address --");
            int n = 0;
            for (Reference r : currentProgram.getReferenceManager().getReferencesTo(a)) {
                out.println("     " + r.getReferenceType() + " from " + r.getFromAddress());
                if (++n >= 30) { out.println("     ..."); break; }
            }

            try {
                DecompileResults dr = di.decompileFunction(f, 180, monitor);
                if (dr != null && dr.decompileCompleted()) {
                    String txt = dr.getDecompiledFunction().getC();
                    if (txt != null && txt.length() > MAX_CHARS) {
                        txt = txt.substring(0, MAX_CHARS) + "\n/* ...truncated... */";
                    }
                    out.println(txt);
                } else {
                    out.println("  (decompile did not complete: "
                                + (dr == null ? "null" : dr.getErrorMessage()) + ")");
                }
            } catch (Exception e) {
                out.println("  (decompile threw: " + e + ")");
            }
            out.println();
            out.println();
        }

        out.flush();
        out.close();
        di.dispose();
        println("DisMenuHandlers -> " + outPath);
    }
}
