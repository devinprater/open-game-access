// DisDecompile3.java -- third pass: the menu manager's actual update function.
//
// WHAT THE FIRST TWO PASSES ESTABLISHED
//   FUN_002489d0 (constructor) registers three thunks:
//       FUN_0036926c -> FUN_00248dd0(DAT_00397770, p)   [event id 1]
//       FUN_00369290 -> FUN_00248dec(DAT_00397770, p)
//       FUN_003692b4 -> FUN_00248ea8(DAT_00397770, p)
//   Those bodies are themselves one-call stubs:
//       FUN_00248dd0() { FUN_0024932c(); }
//   And DAT_00397770 is NOT a pointer -- raw bytes are
//       "00 00 00 00 00 00 00 00 6f 6e 65 30 30 00 00 00" i.e. an inline struct holding "one00".
//   So the menu manager state is a STATIC struct, and these are the update entry points.
//
//   This pass reads the real implementations plus the statics they reference.
//
// USAGE
//   analyzeHeadless <projDir> DISSIDIA_ELF -process EBOOT.dec -noanalysis
//       -scriptPath "<projDir>" -postScript DisDecompile3.java
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

public class DisDecompile3 extends GhidraScript {

    private static final long[] FUNCS = {
        0x0024932cL,   // the real event-1 body
        0x00248decL,
        0x00248ea8L,
    };

    private static final int MAX_CHARS = 16000;

    @Override
    public void run() throws Exception {
        String outPath = System.getProperty("user.home")
                + "/oga-ghidra-dissidia/decompile3-report.txt";
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

        // Dump a wider window of the static struct so its layout is visible.
        out.println("################################################################");
        out.println("### STATIC STRUCT WINDOW around DAT_00397770");
        out.println("################################################################");
        Address base = toAddr(0x00397770L);
        try {
            byte[] b = new byte[256];
            currentProgram.getMemory().getBytes(base, b);
            for (int i = 0; i < b.length; i += 16) {
                StringBuilder hex = new StringBuilder();
                StringBuilder asc = new StringBuilder();
                for (int j = 0; j < 16; j++) {
                    int v = b[i + j] & 0xff;
                    hex.append(String.format("%02x ", v));
                    asc.append((v >= 32 && v < 127) ? (char) v : '.');
                }
                out.println(String.format("  +0x%03x  %s %s", i, hex.toString(), asc.toString()));
            }
        } catch (Exception e) {
            out.println("  (could not read the struct: " + e + ")");
        }

        out.flush();
        out.close();
        di.dispose();
        println("DisDecompile3 -> " + outPath);
    }
}
