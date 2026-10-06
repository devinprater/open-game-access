// DisScreens.java -- for each row of the menu descriptor table (ELF 0x1E7BA0), decompile the
// handler functions and report the string literals each one references, so the 24 menu
// screens can be NAMED from the game's own data instead of guessed.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.symbol.Reference;
import ghidra.app.decompiler.*;
import java.io.PrintWriter;
import java.util.*;

public class DisScreens extends GhidraScript {
    @Override
    public void run() throws Exception {
        PrintWriter w = new PrintWriter(getScriptArgs().length > 0 ? getScriptArgs()[0] : "screens.txt");
        Memory mem = currentProgram.getMemory();
        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);
        FunctionManager fm = currentProgram.getFunctionManager();

        // read the table's function words straight from the listing (static data)
        Address table = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(0x001e7ba0L);
        w.println("=== menu descriptor table @0x1E7BA0 (row stride 0x14) ===");
        for (int r = 0; r < 26; r++) {
            long rowAddr = 0x001e7ba0L + (long) r * 0x14L;
            Address a = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(rowAddr);
            long[] words = new long[5];
            for (int i = 0; i < 5; i++) {
                try { words[i] = mem.getInt(a.add(i * 4)) & 0xffffffffL; } catch (Exception e) { words[i] = -1; }
            }
            if (words[0] == 0 && words[1] == 0 && words[2] == 0 && words[3] == 0) continue;
            w.printf("%n--- row %2d : %08X %08X %08X %08X  count=%d ---%n",
                     r, words[0], words[1], words[2], words[3], words[4]);
            // decompile each word that lands in .text
            Set<Long> fns = new LinkedHashSet<>();
            for (int i = 0; i < 4; i++) if (words[i] >= 0 && words[i] < 0x19f170L) fns.add(words[i]);
            for (long fa : fns) {
                Address fad = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(fa);
                Function f = fm.getFunctionAt(fad);
                if (f == null) { w.printf("    %08X  (no function)%n", fa); continue; }
                // strings this function references (anywhere in the function body)
                TreeSet<String> strs = new TreeSet<>();
                InstructionIterator ii = currentProgram.getListing().getInstructions(f.getBody(), true);
                while (ii.hasNext()) {
                    Instruction ins = ii.next();
                    for (Reference ref : ins.getReferencesFrom()) {
                        Address to = ref.getToAddress();
                        Data d = currentProgram.getListing().getDataAt(to);
                        if (d != null && d.hasStringValue()) {
                            String v = d.getValue() + "";
                            if (v.length() > 1 && v.length() < 70) strs.add(v);
                        }
                    }
                }
                w.printf("    %-9s size=%-6d callers=%-3d  strings=%s%n", f.getName(),
                         f.getBody().getNumAddresses(), f.getCallingFunctions(monitor).size(),
                         strs.isEmpty() ? "(none)" : strs.toString());
            }
        }
        di.dispose();
        w.close();
        println("DisScreens: wrote");
    }
}
