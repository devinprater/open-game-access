// DisLookup.java -- who calls the message lookup, and where does the id come from?
//
// FUN_000d9bdc(id, container) -> text pointer for that id. The runtime hook fires but this
// PPSSPP build will not give reliable register reads at the halt (a0 reads 0xdeadbeef, the
// "unavailable register" sentinel) and cpu.stepping will not resume.
//
// So do it statically: every caller must load the message id into a0 before the call. If one
// caller loads it from a counter, THAT variable is the current line -- readable with a plain
// memory read, no breakpoint and no resume needed.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.Reference;
import ghidra.app.decompiler.*;
import java.io.PrintWriter;
import java.util.*;

public class DisLookup extends GhidraScript {
    @Override
    public void run() throws Exception {
        PrintWriter w = new PrintWriter(getScriptArgs().length > 0 ? getScriptArgs()[0] : "lookup.txt");
        FunctionManager fm = currentProgram.getFunctionManager();
        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);

        long[] targets = new long[]{0xd9bdcL, 0xd9db8L, 0x136c04L};
        Set<Function> callers = new LinkedHashSet<>();

        for (long t : targets) {
            Address a = currentProgram.getImageBase().add(t);
            Function f = fm.getFunctionContaining(a);
            if (f == null) { w.printf("### no function at 0x%06X%n", t); continue; }
            w.printf("=== callers of %s @ %s ===%n", f.getName(), f.getEntryPoint());
            for (Function c : f.getCallingFunctions(monitor)) {
                w.printf("   %s @ %s  size=%d%n", c.getName(), c.getEntryPoint(), c.getBody().getNumAddresses());
                callers.add(c);
            }
            w.println();
        }

        // also: every call site referencing the entry point directly
        w.println("=== direct call sites (references to the entry) ===");
        for (long t : targets) {
            for (long va = t; va <= t; va++) {
                Address a = currentProgram.getImageBase().add(va);
                for (Reference r : getReferencesTo(a)) {
                    Function f = fm.getFunctionContaining(r.getFromAddress());
                    w.printf("   0x%06X -> %s%n", r.getFromAddress().getOffset(),
                             f == null ? "?" : f.getName() + "@" + f.getEntryPoint());
                }
            }
        }

        w.println();
        w.printf("=== decompiling %d caller(s) ===%n", callers.size());
        int n = 0;
        for (Function c : callers) {
            if (n++ >= 10) break;
            w.println("---------------------------------------------------------------");
            w.printf("### %s @ %s  size=%d%n", c.getName(), c.getEntryPoint(), c.getBody().getNumAddresses());
            try { w.println(di.decompileFunction(c, 90, monitor).getDecompiledFunction().getC()); }
            catch (Exception e) { w.println("FAILED: " + e.getMessage()); }
        }
        di.dispose();
        w.close();
        println("DisLookup: wrote");
    }
}
