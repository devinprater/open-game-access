// TagTeamTrace.java — follow the stat chain from the character panel to its source.
//
// WHAT WE KNOW SO FAR: FUN_08a3c458 renders the character STAT PANEL — it calls
// FUN_08840dc4(node, "gauge_attack" | "gauge_defense" | "gauge_technic") to find UI gauge
// nodes, then FUN_08a3c320(panel, index) to obtain each value. So FUN_08a3c320 is the
// accessor that returns a stat, and whatever IT reads is the live character data an
// accessibility adapter should expose.
//
// This script decompiles that chain (and the panel's callers), so the stat layout is read
// out of the game's own code rather than guessed from a RAM sweep.
//
// @category OGA
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.util.task.ConsoleTaskMonitor;
import java.util.*;

public class TagTeamTrace extends GhidraScript {

    void dump(StringBuilder out, DecompInterface di, long addr, int cap) {
        Address a = toAddr(addr);
        Function f = getFunctionAt(a);
        if (f == null) { out.append("\n### no function at 0x").append(Long.toHexString(addr)).append("\n"); return; }
        out.append("\n################ FUNCTION 0x").append(Long.toHexString(addr))
           .append(" ").append(f.getName()).append(" ################\n");
        out.append("  body=").append(f.getBody().getNumAddresses()).append(" bytes\n");
        try {
            DecompileResults dr = di.decompileFunction(f, 90, new ConsoleTaskMonitor());
            if (dr != null && dr.decompileCompleted() && dr.getDecompiledFunction() != null) {
                String c = dr.getDecompiledFunction().getC();
                out.append(c.length() > cap ? c.substring(0, cap) + "\n... [truncated]\n" : c + "\n");
            } else out.append("  (decompile failed)\n");
        } catch (Exception e) { out.append("  (error: ").append(e.getMessage()).append(")\n"); }
    }

    // who calls this function?
    void callers(StringBuilder out, long addr) {
        Function f = getFunctionAt(toAddr(addr));
        if (f == null) return;
        out.append("\n=== callers of FUN_0x").append(Long.toHexString(addr)).append(" ===\n");
        Set<Function> cs = f.getCallingFunctions(new ConsoleTaskMonitor());
        if (cs == null || cs.isEmpty()) { out.append("  (none found)\n"); return; }
        for (Function c : cs)
            out.append("  ").append(c.getEntryPoint()).append("  ").append(c.getName()).append("\n");
    }

    @Override
    public void run() throws Exception {
        StringBuilder out = new StringBuilder();
        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);

        long[] chain = {
            0x08a3c458L,   // the stat panel renderer (known)
            0x08a3c320L,   // stat accessor it calls  <- the important one
            0x08840dc4L,   // node lookup by name (FUN_08840dc4)
            0x08837a0cL,   // some scale/timer helper used in the same panel
        };
        out.append("=== STAT CHAIN ===\n");
        for (long a : chain) dump(out, di, a, 9000);

        for (long a : new long[]{0x08a3c320L, 0x08a3c458L}) callers(out, a);

        // also grab the panel's neighbours, which are usually siblings in the same file
        out.append("\n=== NEIGHBOURING FUNCTIONS (possible siblings) ===\n");
        for (long a = 0x08a3c000L; a < 0x08a3c800L; a += 4) {
            Function f = getFunctionAt(toAddr(a));
            if (f != null && f.getEntryPoint().getOffset() == a)
                out.append("  0x").append(Long.toHexString(a)).append("  ").append(f.getName())
                   .append("  body=").append(f.getBody().getNumAddresses()).append("\n");
        }

        java.io.FileWriter fw = new java.io.FileWriter(
            "C:\\Users\\Devin Prater\\AppData\\Local\\Temp\\tagteam-trace-out.txt");
        fw.write(out.toString());
        fw.close();
        println("wrote " + out.length() + " chars to tagteam-trace-out.txt");
    }
}
