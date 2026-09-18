// DisRenderWriter.java -- locate the function that POPULATES the render-node array, per Codex's trace.
//
// CONTEXT
//   A second agent (Codex) traced the render-block consumer FUN_0024aee0 (the draw callback) and
//   reported that it only READS the pointer triples of the 0x50-strided block array at 0x09DEE480:
//       iVar17 = *(int *)(base + n * 0x50 + 8);
//       iVar18 = *(int *)(base + n * 0x50 + 4);
//       uVar8  = (uint)**(byte **)(base + n * 0x50);
//   and that the best candidate for the WRITER is FUN_0025595c, called once per drawable child
//   immediately before that loop:
//       iVar13 = FUN_0025595c(*puVar16, param_2, iVar13, puVar16[0xb], puVar16);
//       *(int *)(param_1 + 0x1c) += iVar13;
//   Its prediction: a write watchpoint on 0x09DEE480/0x09DEE484 should land in this function.
//
// WHAT THIS SCRIPT DOES
//   Decompiles FUN_0025595c and its immediate callees, printing each function's SIZE first (a size is
//   a cheap classifier) and every store it performs to an address expressed off a parameter -- so a
//   write to the block array's +0x00/+0x04/+0x08 would be visible.
//
//   It also decompiles FUN_00255170 (Codex's secondary candidate) for comparison.
//
// USAGE
//   analyzeHeadless <projDir> DISSIDIA_ELF -process EBOOT.dec -noanalysis
//       -scriptPath "<projDir>" -postScript DisRenderWriter.java
//
// @category OGA

import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.symbol.Reference;

import java.io.File;
import java.io.PrintWriter;
import java.util.LinkedHashMap;
import java.util.LinkedHashSet;
import java.util.Map;
import java.util.Set;

public class DisRenderWriter extends GhidraScript {

    private static final long ROOT = 0x0025595cL;        // Codex's primary writer candidate
    private static final long[] EXTRA = { 0x00255170L }; // secondary candidate
    private static final int MAX_CHARS = 26000;

    private void collect(Function f, int depth, Set<Function> out, Map<Function, Integer> level) {
        if (f == null || out.contains(f) || depth < 0) {
            return;
        }
        out.add(f);
        level.put(f, depth);
        InstructionIterator it = currentProgram.getListing().getInstructions(f.getBody(), true);
        while (it.hasNext()) {
            Instruction ins = it.next();
            for (Reference r : ins.getReferencesFrom()) {
                Address to = r.getToAddress();
                if (to == null) continue;
                Function callee = getFunctionAt(to);
                if (callee != null && !callee.equals(f)) {
                    collect(callee, depth - 1, out, level);
                }
            }
        }
    }

    /** count STORE instructions -- shows whether a function writes anything at all */
    private int stores(Function f) {
        int n = 0;
        InstructionIterator it = currentProgram.getListing().getInstructions(f.getBody(), true);
        while (it.hasNext()) {
            Instruction ins = it.next();
            String mn = ins.getMnemonicString().toLowerCase();
            if (mn.startsWith("s") && (mn.startsWith("sw") || mn.startsWith("sb")
                    || mn.startsWith("sh") || mn.startsWith("sd"))) {
                n++;
            }
        }
        return n;
    }

    @Override
    public void run() throws Exception {
        String outPath = System.getProperty("user.home")
                + "/oga-ghidra-dissidia/render-writer-report.txt";
        PrintWriter out = new PrintWriter(new File(outPath), "UTF-8");

        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);

        Function root = getFunctionAt(toAddr(ROOT));
        Set<Function> fns = new LinkedHashSet<>();
        Map<Function, Integer> level = new LinkedHashMap<>();
        if (root != null) {
            collect(root, 2, fns, level);
        }
        for (long e : EXTRA) {
            Function f = getFunctionAt(toAddr(e));
            if (f != null) {
                fns.add(f);
                level.put(f, 0);
            }
        }

        out.println("################################################################");
        out.println("### RENDER-BLOCK WRITER CANDIDATES -- " + fns.size() + " functions");
        out.println("### size and STORE COUNT first (a producer must store)");
        out.println("################################################################");
        for (Function f : fns) {
            out.println("  d" + level.get(f) + "  " + f.getName() + " @" + f.getEntryPoint()
                        + "  size=" + f.getBody().getNumAddresses()
                        + "  stores=" + stores(f));
        }

        out.println();
        out.println("################################################################");
        out.println("### DECOMPILED BODIES");
        out.println("################################################################");
        for (Function f : fns) {
            out.println();
            out.println("=================================================================");
            out.println("=== " + f.getName() + " @" + f.getEntryPoint()
                        + "  size=" + f.getBody().getNumAddresses()
                        + "  stores=" + stores(f));
            out.println("=================================================================");
            try {
                DecompileResults dr = di.decompileFunction(f, 180, monitor);
                if (dr != null && dr.decompileCompleted()) {
                    String txt = dr.getDecompiledFunction().getC();
                    if (txt != null && txt.length() > MAX_CHARS) {
                        txt = txt.substring(0, MAX_CHARS) + "\n/* ...truncated... */";
                    }
                    out.println(txt);
                } else {
                    out.println("(decompile did not complete)");
                }
            } catch (Exception e) {
                out.println("(decompile threw: " + e + ")");
            }
        }

        out.flush();
        out.close();
        di.dispose();
        println("DisRenderWriter -> " + outPath + " (" + fns.size() + " functions)");
    }
}
