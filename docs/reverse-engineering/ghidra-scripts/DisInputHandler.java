// DisInputHandler.java -- decompile the per-frame input handler and its button consumers.
//
// WHERE THIS STANDS
//   DisFindInput located the input path:
//     FUN_000f7994 calls FUN_0036da44(1) (sceCtrlSetSamplingCycle), FUN_0036da54(0x411b)
//     (sceCtrlSetSamplingMode), then registers FUN_000f790c via FUN_00103db0 on the engine object
//     from FUN_00103c60(DAT_00392cd8) -- the SAME registration mechanism the menu manager uses.
//   So FUN_000f790c is the input callback, and it is the function that turns button bits into game
//   actions. THAT is where a d-pad press becomes a selection change.
//
// WHY THIS MATTERS FOR THE CURSOR
//   Thirty-nine sections cleared every DATA structure reachable from the manager. The one thing never
//   examined is the code that consumes the button. If FUN_000f790c (or its callees) writes a selection
//   index on a d-pad press, then decompiling it names the field -- and the field can then be read in
//   RAM instead of searched for.
//
// WHAT IT EMITS
//   FUN_000f790c and its transitive callees (depth 2), with sizes and callers, so the button-to-action
//   path is visible. Sizes are printed first because a size is a cheap classifier (doc Rule 110).
//
// USAGE
//   analyzeHeadless <projDir> DISSIDIA_ELF -process EBOOT.dec -noanalysis
//       -scriptPath "<projDir>" -postScript DisInputHandler.java
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

public class DisInputHandler extends GhidraScript {

    private static final long ROOT = 0x000f790cL;
    private static final int MAX_CHARS = 26000;

    /** collect callees up to `depth`, preserving discovery order */
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

    @Override
    public void run() throws Exception {
        String outPath = System.getProperty("user.home")
                + "/oga-ghidra-dissidia/input-handler-report.txt";
        PrintWriter out = new PrintWriter(new File(outPath), "UTF-8");

        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);

        Function root = getFunctionAt(toAddr(ROOT));
        if (root == null) {
            out.println("no function at 0x" + Long.toHexString(ROOT));
            out.close();
            println("DisInputHandler: root not found");
            return;
        }

        Set<Function> fns = new LinkedHashSet<>();
        Map<Function, Integer> level = new LinkedHashMap<>();
        collect(root, 2, fns, level);

        out.println("################################################################");
        out.println("### INPUT PATH: " + fns.size() + " functions from FUN_000f790c (depth 2)");
        out.println("### size first -- a size is a cheap classifier (Rule 110)");
        out.println("################################################################");
        for (Function f : fns) {
            Set<Function> callers = new LinkedHashSet<>();
            for (Reference r : currentProgram.getReferenceManager()
                    .getReferencesTo(f.getEntryPoint())) {
                Function c = getFunctionContaining(r.getFromAddress());
                if (c != null) callers.add(c);
            }
            out.println("  d" + level.get(f) + "  " + f.getName() + " @" + f.getEntryPoint()
                        + "  size=" + f.getBody().getNumAddresses()
                        + "  callers=" + callers.size());
        }

        out.println();
        out.println("################################################################");
        out.println("### DECOMPILED BODIES");
        out.println("################################################################");
        for (Function f : fns) {
            out.println();
            out.println("=================================================================");
            out.println("=== " + f.getName() + " @" + f.getEntryPoint()
                        + "  size=" + f.getBody().getNumAddresses());
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
        println("DisInputHandler -> " + outPath + "  (" + fns.size() + " functions)");
    }
}
