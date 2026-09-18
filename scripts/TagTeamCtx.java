// TagTeamCtx.java — resolve the character-select UI context to an ABSOLUTE address.
//
// WHERE WE ARE: FUN_08a02a3c is the character-select panel builder and it reads the selection
// from `*(int *)(param_1 + 0x68)` — a field on its `param_1` UI context pointer. The index is
// real and understood; what is missing is the context's ABSOLUTE RAM address, because param_1
// is passed in and allocated at runtime.
//
// PLAN: find the callers of FUN_08a02a3c and read what they pass. If a caller loads param_1
// from a global, that global IS the context pointer and its address is the answer. If the
// caller is reached from a registry that hands out the UI context, follow one more level.
//
// This also dumps the immediate neighbours in the same file, since UI builders are usually
// registered together.
//
// @category OGA
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.util.task.ConsoleTaskMonitor;
import java.util.*;

public class TagTeamCtx extends GhidraScript {

    void dump(StringBuilder out, DecompInterface di, Address a, int cap) {
        Function f = getFunctionAt(a);
        if (f == null) { out.append("\n### no function at ").append(a).append("\n"); return; }
        out.append("\n################ FUNCTION ").append(a).append(" ################\n");
        out.append("  name=").append(f.getName())
           .append("  body=").append(f.getBody().getNumAddresses())
           .append("  params=").append(f.getParameterCount()).append("\n");
        try {
            DecompileResults dr = di.decompileFunction(f, 90, new ConsoleTaskMonitor());
            if (dr != null && dr.decompileCompleted() && dr.getDecompiledFunction() != null) {
                String c = dr.getDecompiledFunction().getC();
                out.append(c.length() > cap ? c.substring(0, cap) + "\n... [truncated]\n" : c + "\n");
            } else out.append("  (decompile failed)\n");
        } catch (Exception e) { out.append("  (error: ").append(e.getMessage()).append(")\n"); }
    }

    /** show the raw instructions of a function, to catch what the decompiler drops */
    void raw(StringBuilder out, Address entry) {
        Function f = getFunctionAt(entry);
        if (f == null) return;
        out.append("\n=== RAW INSTRUCTIONS of ").append(entry).append(" ===\n");
        Instruction ins = getInstructionAt(f.getEntryPoint());
        int n = 0;
        while (ins != null && f.getBody().contains(ins.getAddress()) && n++ < 120) {
            out.append("  ").append(ins.getAddress()).append("  ").append(ins.toString()).append("\n");
            ins = ins.getNext();
        }
    }

    @Override
    public void run() throws Exception {
        StringBuilder out = new StringBuilder();
        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);

        long TARGET = 0x08a02a3cL;
        Function tf = getFunctionAt(toAddr(TARGET));
        if (tf == null) { println("target function not found"); return; }

        // ---- 1. who calls the panel builder, and what do they pass? ----
        out.append("=== callers of FUN_0x").append(Long.toHexString(TARGET)).append(" ===\n");
        Set<Function> cs = tf.getCallingFunctions(new ConsoleTaskMonitor());
        List<Address> callerAddrs = new ArrayList<>();
        if (cs == null || cs.isEmpty()) {
            out.append("  (no callers found — may be dispatched through a table)\n");
        } else {
            for (Function c : cs) {
                out.append("  ").append(c.getEntryPoint()).append("  ").append(c.getName())
                   .append("  body=").append(c.getBody().getNumAddresses()).append("\n");
                callerAddrs.add(c.getEntryPoint());
            }
        }

        // ---- 2. direct references to the target's address (function-pointer tables) ----
        out.append("\n=== references to the function address (dispatch tables) ===\n");
        ReferenceIterator it = currentProgram.getReferenceManager().getReferencesTo(toAddr(TARGET));
        int shown = 0;
        while (it.hasNext() && shown++ < 40) {
            Reference r = it.next();
            Function f = getFunctionContaining(r.getFromAddress());
            out.append("  ").append(r.getReferenceType()).append(" from ").append(r.getFromAddress())
               .append(f == null ? "" : "  in " + f.getName()).append("\n");
        }

        // ---- 3. decompile the callers, and the raw body of the target ----
        if (!callerAddrs.isEmpty()) {
            out.append("\n=== DECOMPILED CALLERS ===\n");
            for (Address a : callerAddrs) dump(out, di, a, 9000);
        }
        raw(out, toAddr(TARGET));

        // ---- 4. neighbours in the same file ----
        out.append("\n=== NEIGHBOURS (0x08A02000..0x08A2A000) ===\n");
        for (long a = 0x08a02000L; a < 0x08a2a000L; a += 4) {
            Function f = getFunctionAt(toAddr(a));
            if (f != null && f.getEntryPoint().getOffset() == a)
                out.append("  0x").append(Long.toHexString(a)).append("  ").append(f.getName())
                   .append("  body=").append(f.getBody().getNumAddresses()).append("\n");
        }

        java.io.FileWriter fw = new java.io.FileWriter(
            "C:\\Users\\Devin Prater\\AppData\\Local\\Temp\\tagteam-ctx-out.txt");
        fw.write(out.toString());
        fw.close();
        println("wrote " + out.length() + " chars to tagteam-ctx-out.txt");
    }
}
