// DisDecompile2.java -- second pass: the menu manager's real entry points.
//
// WHAT THE FIRST PASS ESTABLISHED
//   The three "callbacks" registered by FUN_002489d0 are 36-byte THUNKS that just forward to real
//   implementations, passing a single GLOBAL singleton:
//
//       FUN_0036926c  ->  FUN_00248dd0(DAT_00397770, param_1)
//       FUN_00369290  ->  FUN_00248dec(DAT_00397770, param_1)
//       FUN_003692b4  ->  FUN_00248ea8(DAT_00397770, param_1)
//
//   and the "singleton getter" FUN_00103c60 is a one-liner:  return *(u32*)(param_1 + 0x2000);
//
//   So the live menu state hangs off DAT_00397770. These are the functions to read, plus the getter's
//   three siblings that were registered alongside it.
//
// USAGE
//   analyzeHeadless <projDir> DISSIDIA_ELF -process EBOOT.dec -noanalysis
//       -scriptPath "<projDir>" -postScript DisDecompile2.java
//
// @category OGA

import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.Function;

import java.io.PrintWriter;
import java.io.File;

public class DisDecompile2 extends GhidraScript {

    private static final long[] FUNCS = {
        0x00248dd0L,   // the event-1 handler body
        0x00248decL,
        0x00248ea8L,
        0x002489d0L,   // re-read the constructor to see what DAT_00397770 receives
    };

    private static final long[] DATAS = {
        0x00397770L,   // the menu singleton the thunks pass
        0x00392cd8L,   // the key passed to FUN_00103c60
    };

    private static final int MAX_CHARS = 14000;

    @Override
    public void run() throws Exception {
        String outPath = System.getProperty("user.home")
                + "/oga-ghidra-dissidia/decompile2-report.txt";
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
            try {
                DecompileResults dr = di.decompileFunction(f, 120, monitor);
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

        for (long addr : DATAS) {
            Address a = toAddr(addr);
            out.println("################################################################");
            out.println("### DATA " + a);
            out.println("################################################################");
            Data d = getDataAt(a);
            out.println("  defined data: " + (d == null ? "(none)" : d.getDataType().getName() + " = " + d.getValue()));

            // Show the raw 16 bytes there so a pointer or a struct head is visible either way.
            try {
                byte[] b = new byte[16];
                currentProgram.getMemory().getBytes(a, b);
                StringBuilder sb = new StringBuilder("  raw: ");
                for (byte x : b) sb.append(String.format("%02x ", x));
                out.println(sb.toString());
            } catch (Exception e) {
                out.println("  (could not read bytes: " + e + ")");
            }
            out.println();
        }

        out.flush();
        out.close();
        di.dispose();
        println("DisDecompile2 -> " + outPath);
    }
}
