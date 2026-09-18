// DisSelectRefs.java -- find functions that reference BOTH the menu static and the pad object.
//
// THE DECISIVE QUERY
//   The render path is now understood:
//     FUN_0025595c writes render blocks at  (*(int*)(DAT_00397770+0x14) + n*0x50)
//     and increments the count at DAT_00397770+0x18 -- the 0x50-strided array.
//     The heap manager object (0x08C08EB0) has +0x14 = 0x09DEE3C0, which IS that base; the block
//     whose pointers alternate with the highlight is base + 3*0x50.
//     FUN_0024aee0 (the draw callback) only READS those blocks.
//   So the renderer is driven by something upstream. The selection must be computed by a routine that
//   (a) knows the menu and (b) consumes pad state.
//
//   That combination is searchable: list every function referencing the menu static DAT_00397770,
//   every function referencing the pad object DAT_003925b0, and report the INTERSECTION. A function
//   touching both is the most likely place a d-pad press becomes a menu selection change.
//
// USAGE
//   analyzeHeadless <projDir> DISSIDIA_ELF -process EBOOT.dec -noanalysis
//       -scriptPath "<projDir>" -postScript DisSelectRefs.java
//
// @category OGA

import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.symbol.Reference;

import java.io.File;
import java.io.PrintWriter;
import java.util.LinkedHashSet;
import java.util.Set;
import java.util.TreeMap;

public class DisSelectRefs extends GhidraScript {

    private static final long MENU_STATIC = 0x00397770L;   // DAT_00397770 (chapter table + render ptrs)
    private static final long PAD_OBJECT  = 0x003925b0L;   // DAT_003925b0 (global pad object)
    private static final long MANAGER_PTR_FIELD = 0x00397770L;
    private static final int MAX_CHARS = 12000;

    private Set<Function> refsTo(long addr) {
        Set<Function> out = new LinkedHashSet<>();
        for (Reference r : currentProgram.getReferenceManager().getReferencesTo(toAddr(addr))) {
            Function f = getFunctionContaining(r.getFromAddress());
            if (f != null) {
                out.add(f);
            }
        }
        return out;
    }

    /** also catch functions whose instruction operands mention the address without a recorded ref */
    private Set<Function> bodyMentions(long addr) {
        Set<Function> out = new LinkedHashSet<>();
        Address a = toAddr(addr);
        FunctionIterator fit = currentProgram.getFunctionManager().getFunctions(true);
        while (fit.hasNext()) {
            Function f = fit.next();
            InstructionIterator iit = currentProgram.getListing().getInstructions(f.getBody(), true);
            while (iit.hasNext()) {
                Instruction ins = iit.next();
                for (Reference r : ins.getReferencesFrom()) {
                    Address to = r.getToAddress();
                    if (to != null && to.equals(a)) {
                        out.add(f);
                        break;
                    }
                }
            }
        }
        return out;
    }

    @Override
    public void run() throws Exception {
        String outPath = System.getProperty("user.home")
                + "/oga-ghidra-dissidia/select-refs-report.txt";
        PrintWriter out = new PrintWriter(new File(outPath), "UTF-8");
        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);

        Set<Function> menuRefs = refsTo(MENU_STATIC);
        Set<Function> padRefs = refsTo(PAD_OBJECT);
        menuRefs.addAll(bodyMentions(MENU_STATIC));
        padRefs.addAll(bodyMentions(PAD_OBJECT));

        out.println("################################################################");
        out.println("### FUNCTIONS REFERENCING DAT_00397770 (menu static): " + menuRefs.size());
        out.println("################################################################");
        TreeMap<Long, Function> byAddr = new TreeMap<>();
        for (Function f : menuRefs) {
            byAddr.put(f.getEntryPoint().getOffset(), f);
        }
        for (Function f : byAddr.values()) {
            out.println("   " + f.getName() + " @" + f.getEntryPoint()
                        + "  size=" + f.getBody().getNumAddresses()
                        + (padRefs.contains(f) ? "   <== ALSO REFERENCES THE PAD" : ""));
        }

        out.println();
        out.println("################################################################");
        out.println("### FUNCTIONS REFERENCING DAT_003925b0 (pad object): " + padRefs.size());
        out.println("################################################################");
        byAddr.clear();
        for (Function f : padRefs) {
            byAddr.put(f.getEntryPoint().getOffset(), f);
        }
        for (Function f : byAddr.values()) {
            out.println("   " + f.getName() + " @" + f.getEntryPoint()
                        + "  size=" + f.getBody().getNumAddresses()
                        + (menuRefs.contains(f) ? "   <== ALSO REFERENCES THE MENU" : ""));
        }

        Set<Function> both = new LinkedHashSet<>(menuRefs);
        both.retainAll(padRefs);
        out.println();
        out.println("################################################################");
        out.println("### INTERSECTION -- reference BOTH: " + both.size());
        out.println("################################################################");

        for (Function f : both) {
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
        println("DisSelectRefs -> " + outPath + "  (menu=" + menuRefs.size()
                + " pad=" + padRefs.size() + " both=" + both.size() + ")");
    }
}
