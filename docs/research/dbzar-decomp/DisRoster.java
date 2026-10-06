// DisRoster.java -- who reads the roster table? ELF address of the table found live at
// RAM 0x08A243F0 is 0x1E03F0 (delta 0x08804000). Finding its readers should expose the
// character-select logic and any per-character unlock/availability flag.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.scalar.Scalar;
import ghidra.app.decompiler.*;
import java.io.PrintWriter;
import java.util.*;

public class DisRoster extends GhidraScript {
    @Override
    public void run() throws Exception {
        PrintWriter w = new PrintWriter(getScriptArgs().length > 0 ? getScriptArgs()[0] : "roster.txt");
        Listing listing = currentProgram.getListing();
        FunctionManager fm = currentProgram.getFunctionManager();
        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);

        // The table is reached by lui/addiu. Search instructions for the immediate pair that
        // builds 0x1E03F0: lui 0x1E with addiu 0x3F0, or any operand in 0x1E0300-0x1E0500.
        w.println("=== instructions with an immediate near 0x1E03F0 (the roster table) ===");
        Set<Function> readers = new LinkedHashSet<>();
        InstructionIterator it = listing.getInstructions(true);
        int shown = 0;
        while (it.hasNext() && shown < 300) {
            Instruction ins = it.next();
            for (int op = 0; op < ins.getNumOperands(); op++) {
                for (Object o : ins.getOpObjects(op)) {
                    if (o instanceof Scalar) {
                        long v = ((Scalar) o).getUnsignedValue();
                        if (v >= 0x1E03C0L && v <= 0x1E0420L) {
                            Function f = fm.getFunctionContaining(ins.getAddress());
                            w.printf("  %s  %-34s %s%n", ins.getAddress(), ins.toString(),
                                     f == null ? "?" : f.getName() + "@" + f.getEntryPoint());
                            if (f != null) readers.add(f);
                            shown++;
                        }
                    }
                }
            }
        }
        w.printf("  (%d sites)%n%n", shown);

        w.println("=== functions touching the roster table ===");
        for (Function f : readers) {
            w.printf("  %s @ %s  size=%d  callers=%d%n", f.getName(), f.getEntryPoint(),
                     f.getBody().getNumAddresses(), f.getCallingFunctions(monitor).size());
        }
        w.println();

        // also: strings about becoming available, and who references them
        w.println("=== who references the unlock messages ===");
        for (String s : new String[]{"%s has become available!", "%s has reached a new", "A new scenario"}) {
            byte[] needle = s.getBytes("US-ASCII");
            Address cur = currentProgram.getMemory().getMinAddress();
            int n = 0;
            while (n < 2) {
                Address found = currentProgram.getMemory().findBytes(cur, currentProgram.getMemory().getMaxAddress(), needle, null, true, monitor);
                if (found == null) break;
                w.printf("  %-28s @ %s%n", s, found);
                for (ghidra.program.model.symbol.Reference r : getReferencesTo(found)) {
                    Function f = fm.getFunctionContaining(r.getFromAddress());
                    w.printf("      ref from %s in %s%n", r.getFromAddress(), f == null ? "?" : f.getName() + "@" + f.getEntryPoint());
                    if (f != null) readers.add(f);
                }
                n++; cur = found.add(1);
            }
        }

        w.println();
        w.println("=== decompile the roster readers (first 8) ===");
        int k = 0;
        for (Function f : readers) {
            if (k++ >= 8) break;
            w.println("---------------------------------------------------------------");
            w.printf("### %s @ %s  size=%d%n", f.getName(), f.getEntryPoint(), f.getBody().getNumAddresses());
            try { w.println(di.decompileFunction(f, 60, monitor).getDecompiledFunction().getC()); }
            catch (Exception e) { w.println("FAILED: " + e.getMessage()); }
        }
        di.dispose();
        w.close();
        println("DisRoster: wrote");
    }
}
