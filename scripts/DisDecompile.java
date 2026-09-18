// DisDecompile.java -- decompile a fixed list of functions/globals by address.
//
// WHY
//   Section 16 of the Dissidia notes ends with a small, precise task: read the three per-frame menu
//   callbacks registered by the menu manager constructor (FUN_002489d0):
//
//       FUN_0036926c   registered on event id 1   <- likely update/execute hook
//       FUN_00369290
//       FUN_003692b4
//
//   plus the singleton getter and its key global:
//
//       FUN_00103c60(DAT_00392cd8)   -> the object the callbacks are registered against
//
//   Reading those is what locates the selection index, which by section 16's finding is a field of an
//   ALLOCATED struct (param_1[8] = -1 at init) behind a runtime pointer -- unreachable by RAM scanning.
//
// USAGE
//   analyzeHeadless <projDir> DISSIDIA_ELF -process EBOOT.dec -noanalysis
//       -scriptPath "<projDir>" -postScript DisDecompile.java
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
import java.util.*;

public class DisDecompile extends GhidraScript {

    /** Addresses to decompile as functions. */
    private static final long[] FUNCS = {
        0x0036926cL,
        0x00369290L,
        0x003692b4L,
        0x00103c60L,
        0x00247b40L,
    };

    /** Data addresses to inspect. */
    private static final long[] DATAS = {
        0x00392cd8L,
    };

    private static final int MAX_CHARS = 12000;

    @Override
    public void run() throws Exception {
        String outPath = System.getProperty("user.home")
                + "/oga-ghidra-dissidia/decompile-report.txt";
        PrintWriter out = new PrintWriter(new File(outPath), "UTF-8");

        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);

        for (long addr : FUNCS) {
            Address a = toAddr(addr);
            out.println("################################################################");
            out.println("### FUNCTION " + a);
            out.println("################################################################");

            Function f = getFunctionAt(a);
            if (f == null) {
                f = getFunctionContaining(a);
            }
            if (f == null) {
                out.println("  (no function defined at or containing this address)");
                // Try to at least disassemble a window so the bytes are visible.
                out.println("  disassembly window:");
                Address cur = a;
                for (int i = 0; i < 40; i++) {
                    ghidra.program.model.listing.Instruction ins = getInstructionAt(cur);
                    if (ins == null) break;
                    out.println("     " + cur + "  " + ins.toString());
                    cur = cur.add(ins.getLength());
                }
                out.println();
                continue;
            }

            out.println("  function: " + f.getName() + " @ " + f.getEntryPoint()
                        + "   size=" + f.getBody().getNumAddresses());

            // callers, to place it in the call graph
            Set<Long> callers = new HashSet<>();
            ReferenceIterator refs = currentProgram.getReferenceManager().getReferencesTo(f.getEntryPoint());
            while (refs.hasNext()) {
                Reference r = refs.next();
                callers.add(r.getFromAddress().getOffset());
            }
            out.println("  callers (" + callers.size() + "): ");
            for (Long c : callers) {
                Function cf = getFunctionContaining(toAddr(c));
                out.println("     " + toAddr(c) + (cf == null ? "" : "  in " + cf.getName()));
            }
            out.println();

            try {
                DecompileResults dr = di.decompileFunction(f, 120, monitor);
                if (dr != null && dr.decompileCompleted()) {
                    String txt = dr.getDecompiledFunction().getC();
                    if (txt.length() > MAX_CHARS) {
                        txt = txt.substring(0, MAX_CHARS) + "\n/* ...truncated... */";
                    }
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
            if (d != null) {
                out.println("  type=" + d.getDataType().getName() + "  value=" + d.getValue());
            } else {
                out.println("  (no defined data)");
            }
            try {
                out.println("  pointers TO it:");
                ReferenceIterator refs = currentProgram.getReferenceManager().getReferencesTo(a);
                int n = 0;
                while (refs.hasNext() && n < 20) {
                    Reference r = refs.next();
                    Function cf = getFunctionContaining(r.getFromAddress());
                    out.println("     from " + r.getFromAddress()
                                + (cf == null ? "" : "   in " + cf.getName()));
                    n++;
                }
                if (n == 0) out.println("     (none)");
            } catch (Exception e) {
                out.println("  (refs threw: " + e + ")");
            }
            out.println();
        }

        out.flush();
        out.close();
        di.dispose();
        println("DisDecompile -> " + outPath);
    }
}
