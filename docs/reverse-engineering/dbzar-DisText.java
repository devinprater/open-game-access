// DisText.java -- JOB B: the MSG-ID -> text API.
//
// Found: FUN_0011fbf0 sets puRam0009f52c = &DAT_00220200 (the "#MSG" container) and calls
// FUN_000da040("#MSG"). FUN_000a5860 (font waiter) calls FUN_000d9bdc(i, table) then
// FUN_00136c04(0, 0x22, result) for i in 0..3. Those are the loader and the id->string path.
//
// This decompiles the whole cluster and finds everything referencing the containers.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.Reference;
import ghidra.app.decompiler.*;
import java.io.PrintWriter;
import java.util.*;

public class DisText extends GhidraScript {
    @Override
    public void run() throws Exception {
        PrintWriter w = new PrintWriter(getScriptArgs().length > 0 ? getScriptArgs()[0] : "text.txt");
        FunctionManager fm = currentProgram.getFunctionManager();
        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);

        w.println("=== JOB A/B small helpers + the chapter-select calls ===");
        long[] targets = new long[]{
            0xda040L, 0xd9bdcL, 0xd98b0L, 0x136c04L, 0x136de0L, 0x12665cL, 0x12500cL,
            0x126d08L, 0xda040L, 0x140304L, 0x19f880L, 0x11e434L, 0x12378cL,
            0x96ecL, 0x31484L,                // JOB A: the two tiny chapter-select helpers
            0x8590L, 0x314a8L, 0x11e3c8L, 0x11fd84L
        };
        Set<Long> done = new LinkedHashSet<>();
        for (long t : targets) {
            if (!done.add(t)) continue;
            Address a = currentProgram.getImageBase().add(t);
            Function f = fm.getFunctionContaining(a);
            if (f == null) { w.printf("%n### no function at 0x%06X%n", t); continue; }
            w.println("---------------------------------------------------------------");
            w.printf("### %s @ %s  size=%d  callers=%d%n", f.getName(), f.getEntryPoint(),
                     f.getBody().getNumAddresses(), f.getCallingFunctions(monitor).size());
            try { w.println(di.decompileFunction(f, 90, monitor).getDecompiledFunction().getC()); }
            catch (Exception e) { w.println("FAILED: " + e.getMessage()); }
        }

        // who references the message containers?
        w.println();
        w.println("=== references to the message containers ===");
        for (long va : new long[]{0x220200L, 0x268b40L, 0x220530L, 0x221e40L, 0x20eb58L, 0x20eb54L,
                                  0x20eb5cL, 0x9f538L, 0x9f52cL, 0xaa8eaL}) {
            Address a = currentProgram.getImageBase().add(va);
            w.printf("  --- vaddr 0x%06X ---%n", va);
            int n = 0;
            for (Reference r : getReferencesTo(a)) {
                Function f = fm.getFunctionContaining(r.getFromAddress());
                w.printf("      from %s  %s%n", r.getFromAddress(),
                         f == null ? "?" : (f.getName() + "@" + f.getEntryPoint()));
                if (++n > 16) { w.println("      ..."); break; }
            }
            if (n == 0) w.println("      (none)");
        }
        di.dispose();
        w.close();
        println("DisText: wrote");
    }
}
