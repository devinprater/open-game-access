// DisDecompile4.java -- final pass: the two functions the menu dispatcher actually calls.
//
// WHERE THIS STANDS
//   FUN_0024932c (the real update, section 17) walks two item lists and calls exactly two functions:
//
//       FUN_0025468c()                 -- when a node is actionable (flags bit0 set, bit1 clear)
//       FUN_0024910c(param_1, iVar2)   -- when a node's +0x17 counter exceeds 2
//
//   Those two ARE the selection/activation logic. Reading them is what closes the cursor question.
//   Also resolve DAT_00392d10, the config object the dispatcher reads (+0x18) for a float value.
//
// USAGE
//   analyzeHeadless <projDir> DISSIDIA_ELF -process EBOOT.dec -noanalysis
//       -scriptPath "<projDir>" -postScript DisDecompile4.java
//
// @category OGA

import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;

import java.io.PrintWriter;
import java.io.File;

public class DisDecompile4 extends GhidraScript {

    private static final long[] FUNCS = {
        0x0024910cL,   // per-item activation
        0x0025468cL,   // actionable-node handler
    };

    private static final long[] DATAS = {
        0x00392d10L,   // config object read at +0x18
    };

    private static final int MAX_CHARS = 16000;

    @Override
    public void run() throws Exception {
        String outPath = System.getProperty("user.home")
                + "/oga-ghidra-dissidia/decompile4-report.txt";
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

            int nc = 0;
            ReferenceIterator refs = currentProgram.getReferenceManager().getReferencesTo(f.getEntryPoint());
            StringBuilder cb = new StringBuilder();
            while (refs.hasNext() && nc < 12) {
                Reference r = refs.next();
                Function cf = getFunctionContaining(r.getFromAddress());
                cb.append("      ").append(r.getFromAddress().toString())
                  .append(cf == null ? "" : "  in " + cf.getName()).append("\n");
                nc++;
            }
            out.println("  callers (" + nc + "):");
            out.print(cb.toString());
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

        for (long addr : DATAS) {
            Address a = toAddr(addr);
            out.println("################################################################");
            out.println("### DATA " + a);
            out.println("################################################################");
            try {
                byte[] b = new byte[32];
                currentProgram.getMemory().getBytes(a, b);
                StringBuilder sb = new StringBuilder("  raw: ");
                for (byte x : b) sb.append(String.format("%02x ", x));
                out.println(sb.toString());
            } catch (Exception e) {
                out.println("  (could not read: " + e + ")");
            }
            out.println("  pointers TO it:");
            ReferenceIterator refs = currentProgram.getReferenceManager().getReferencesTo(a);
            int n = 0;
            while (refs.hasNext() && n < 10) {
                Reference r = refs.next();
                Function cf = getFunctionContaining(r.getFromAddress());
                out.println("     " + r.getFromAddress() + (cf == null ? "" : "   in " + cf.getName()));
                n++;
            }
            if (n == 0) out.println("     (none)");
            out.println();
        }

        out.flush();
        out.close();
        di.dispose();
        println("DisDecompile4 -> " + outPath);
    }
}
