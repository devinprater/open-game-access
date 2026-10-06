// DisHandlers.java -- decompile every menu-screen handler and report (a) its callees, so the
// common text/UI helper stands out, and (b) the immediate constants it passes, which is where
// a message id would appear.
//
// Row layout of the descriptor table (ELF 0x1E7BA0, stride 0x14):
//   w0,w1,w2,w3 = handler function pointers, w4 = a small count.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.scalar.Scalar;
import ghidra.app.decompiler.*;
import java.io.PrintWriter;
import java.util.*;

public class DisHandlers extends GhidraScript {
    @Override
    public void run() throws Exception {
        PrintWriter w = new PrintWriter(getScriptArgs().length > 0 ? getScriptArgs()[0] : "handlers.txt");
        Memory mem = currentProgram.getMemory();
        FunctionManager fm = currentProgram.getFunctionManager();
        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);

        // gather every handler address from the table
        LinkedHashSet<Long> handlers = new LinkedHashSet<>();
        for (int r = 0; r < 26; r++) {
            long row = 0x001e7ba0L + (long) r * 0x14L;
            Address a = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(row);
            for (int i = 0; i < 4; i++) {
                long v;
                try { v = mem.getInt(a.add(i * 4)) & 0xffffffffL; } catch (Exception e) { continue; }
                if (v > 0 && v < 0x19f170L) handlers.add(v);
            }
        }
        w.println("handlers found: " + handlers.size());

        // callee histogram across all handlers
        Map<String, Integer> calleeCount = new TreeMap<>();
        Map<Long, List<Long>> handlerImms = new LinkedHashMap<>();

        for (long h : handlers) {
            Address fad = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(h);
            Function f = fm.getFunctionAt(fad);
            if (f == null) continue;
            List<Long> imms = new ArrayList<>();
            InstructionIterator ii = currentProgram.getListing().getInstructions(f.getBody(), true);
            while (ii.hasNext()) {
                Instruction ins = ii.next();
                for (int op = 0; op < ins.getNumOperands(); op++) {
                    for (Object o : ins.getOpObjects(op)) {
                        if (o instanceof Scalar) {
                            long v = ((Scalar) o).getUnsignedValue();
                            imms.add(v);
                        }
                    }
                }
                if (ins.getFlowType().isCall()) {
                    for (ghidra.program.model.symbol.Reference ref : ins.getReferencesFrom()) {
                        Function cf = fm.getFunctionAt(ref.getToAddress());
                        String nm = cf != null ? cf.getName() : ("sub_" + ref.getToAddress());
                        calleeCount.merge(nm, 1, Integer::sum);
                    }
                }
            }
            handlerImms.put(h, imms);
        }

        w.println();
        w.println("=== callees across all screen handlers (count desc) ===");
        List<Map.Entry<String, Integer>> sorted = new ArrayList<>(calleeCount.entrySet());
        sorted.sort((x, y) -> y.getValue() - x.getValue());
        for (Map.Entry<String, Integer> e : sorted) {
            if (e.getValue() >= 3) w.printf("  %-24s called by %d handlers%n", e.getKey(), e.getValue());
        }

        w.println();
        w.println("=== per handler: immediates in 0x1000-0x20000 (plausible message ids) ===");
        for (Map.Entry<Long, List<Long>> e : handlerImms.entrySet()) {
            List<Long> msgs = new ArrayList<>();
            for (long v : e.getValue()) if (v >= 0x1000 && v <= 0x20000) msgs.add(v);
            w.printf("  handler %08X : %s%n", e.getKey(), msgs.isEmpty() ? "(none)" : msgs.toString());
        }

        // full decompile of the first few handlers so the text call is visible in context
        w.println();
        w.println("=== full decompile: the first 6 handlers ===");
        int n = 0;
        for (long h : handlers) {
            if (n++ >= 6) break;
            Address fad = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(h);
            Function f = fm.getFunctionAt(fad);
            if (f == null) continue;
            w.println("---------------------------------------------------------------");
            w.printf("### %s @ %s  size=%d%n", f.getName(), f.getEntryPoint(), f.getBody().getNumAddresses());
            try { w.println(di.decompileFunction(f, 60, monitor).getDecompiledFunction().getC()); }
            catch (Exception ex) { w.println("FAILED: " + ex.getMessage()); }
        }
        di.dispose();
        w.close();
        println("DisHandlers: wrote");
    }
}
