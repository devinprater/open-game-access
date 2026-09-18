// TagTeamNames.java — find the code that indexes the resident character-name table.
//
// WHY: fighter identity is NOT in RAM. Proven:
//   - no pointer to the roster entry "Goku" (0x08C85C82)
//   - ZERO pointers anywhere into 0x08C85C00-0x08C86600, on a dump with "Frieza" on screen
//   - strings are packed variable-length (deltas 0xA, 0x14, 0x16, 0xC, 0x1E)
// => the game computes the offset from a base IN CODE. Only the disassembly can show it.
//
// This script finds:
//   1. instructions that load or reference the name-table base region
//   2. the functions that service the chara_name_01 / chara_name_02 HUD elements
//      (0x08A733AC / 0x08A73384 in the RAM descriptor table)
//   3. prints their decompilation so the index -> offset scheme is readable
//
// MIPS: the base is a 32-bit constant, so look for LUI/ORI pairs and for any operand
// whose value lands in the name region.

import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.scalar.Scalar;
import ghidra.program.model.symbol.*;

import java.io.*;
import java.util.*;

public class TagTeamNames extends GhidraScript {

    // the resident text tables, as observed in live RAM
    static final long NAME_LO = 0x08C85000L;
    static final long NAME_HI = 0x08C87200L;
    static final long ROSTER_A = 0x08C85C82L;   // roster copy A (Goku first)
    static final long ROSTER_B = 0x08C8613AL;   // roster copy B
    static final long CHARA_01 = 0x08A733ACL;   // chara_name_01 descriptor
    static final long CHARA_02 = 0x08A73384L;

    StringBuilder out = new StringBuilder();
    DecompInterface dec = new DecompInterface();

    void log(String s) { out.append(s).append("\n"); }

    @Override
    public void run() throws Exception {
        dec.openProgram(currentProgram);

        log("=== TagTeamNames: who indexes the character-name table? ===");
        log("name region  : 0x" + Long.toHexString(NAME_LO) + " - 0x" + Long.toHexString(NAME_HI));
        log("roster A     : 0x" + Long.toHexString(ROSTER_A));
        log("chara_name_01: 0x" + Long.toHexString(CHARA_01));
        log("");

        // ---- 1. scan ALL instructions for operands landing in the name region ----
        log("=== instructions with an operand inside the name region ===");
        Set<Function> funcs = new LinkedHashSet<>();
        int n = 0;
        Listing lst = currentProgram.getListing();
        InstructionIterator it = lst.getInstructions(true);
        while (it.hasNext()) {
            Instruction ins = it.next();
            for (int op = 0; op < ins.getNumOperands(); op++) {
                Object[] objs = ins.getOpObjects(op);
                for (Object o : objs) {
                    long v = -1;
                    if (o instanceof Scalar) v = ((Scalar) o).getUnsignedValue();
                    else if (o instanceof Address) v = ((Address) o).getOffset();
                    if (v >= NAME_LO && v < NAME_HI) {
                        log("  " + ins.getAddress() + "  " + ins.toString() + "   (op=" + op + " val=0x" + Long.toHexString(v) + ")");
                        Function f = getFunctionContaining(ins.getAddress());
                        if (f != null) funcs.add(f);
                        n++;
                    }
                }
            }
        }
        log("  total: " + n + " instructions, " + funcs.size() + " distinct functions");
        log("");

        // ---- 2. who references the chara_name descriptor addresses? ----
        log("=== references to the chara_name descriptors ===");
        for (long target : new long[]{CHARA_01, CHARA_02, ROSTER_A, ROSTER_B}) {
            Address a = toAddr(target);
            Reference[] refs = getReferencesTo(a);
            log("  0x" + Long.toHexString(target) + " : " + refs.length + " reference(s)");
            for (Reference r : refs) {
                log("      from " + r.getFromAddress() + "  type=" + r.getReferenceType());
                Function f = getFunctionContaining(r.getFromAddress());
                if (f != null) funcs.add(f);
            }
        }
        log("");

        // ---- 3. decompile the candidate functions ----
        log("=== decompilation of " + funcs.size() + " candidate function(s) ===");
        int shown = 0;
        for (Function f : funcs) {
            if (shown >= 12) { log("  ... (more omitted)"); break; }
            log("");
            log("################ FUNCTION " + f.getName() + " @ " + f.getEntryPoint()
                + "  body=" + f.getBody().getNumAddresses() + " bytes ################");
            try {
                DecompileResults dr = dec.decompileFunction(f, 60, monitor);
                if (dr != null && dr.decompileCompleted()) {
                    String c = dr.getDecompiledFunction().getC();
                    log(c.length() > 4000 ? c.substring(0, 4000) + "\n... [truncated]" : c);
                } else {
                    log("  (decompilation did not complete)");
                }
            } catch (Exception e) {
                log("  (decompile error: " + e.getMessage() + ")");
            }
            shown++;
        }

        // ---- write ----
        String path = System.getProperty("user.dir") + File.separator + "tagteam-names-out.txt";
        try (PrintWriter pw = new PrintWriter(new FileWriter(path))) {
            pw.print(out);
        }
        println("TagTeamNames.java> wrote " + out.length() + " chars to tagteam-names-out.txt");
    }
}
