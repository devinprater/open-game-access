// DisMenu2.java -- the menu state struct's fields, its descriptor tables, and every
// writer of the SELECTION field, so the mechanism is read from code not guessed.
//
// Findings from DisMenu.java that this follows up:
//   menu struct at 0xC139C (fixed):  +0x70 = screen/menu id,  +0x74 = selected item (-1 = none)
//   descriptor table DAT_001e7ba0 + id*0x14
//   FUN_000e2604 indexes DAT_001e7bb0 + selId*0x14
//
// Strategy: reference-scan the whole listing for the struct base and for each interesting
// offset, and decompile the functions that touch +0x70 / +0x74.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.scalar.Scalar;
import ghidra.program.model.symbol.Reference;
import ghidra.app.decompiler.*;
import java.io.PrintWriter;
import java.util.*;

public class DisMenu2 extends GhidraScript {
    private static final long STRUCT = 0x000c139cL;   // menu state struct base
    private static final long DESC_TABLE = 0x001e7ba0L;

    @Override
    public void run() throws Exception {
        String out = getScriptArgs().length > 0 ? getScriptArgs()[0] : "menu2.txt";
        PrintWriter w = new PrintWriter(out);
        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);
        FunctionManager fm = currentProgram.getFunctionManager();
        Listing listing = currentProgram.getListing();

        // ---- 1. raw bytes of the descriptor tables (static data, readable in the ELF) ----
        w.println("=== static tables ===");
        for (long base : new long[]{DESC_TABLE, 0x001e7bb0L}) {
            w.printf("--- table @ %08X (0x80 bytes) ---%n", base);
            for (int row = 0; row < 8; row++) {
                long a = base + row * 0x14L;
                StringBuilder sb = new StringBuilder();
                sb.append(String.format("  +%02X  ", row * 0x14));
                for (int i = 0; i < 0x14; i++) {
                    Address ad = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(a + i);
                    Data d = listing.getDataAt(ad);
                    int b = d != null ? d.getInt(0) & 0xff : (currentProgram.getMemory().getInt(ad) & 0xff);
                    sb.append(String.format("%02X ", b));
                }
                w.println(sb.toString());
            }
        }
        w.println();

        // ---- 2. every reference to the struct base, and to base+0x70 / +0x74 ----
        String[] names = {"STRUCT 0xC139C", "+0x70 (screen id)", "+0x74 (selection)"};
        long[] targets = {STRUCT, STRUCT + 0x70, STRUCT + 0x74, STRUCT + 0x6c, STRUCT + 0x9a0};
        Set<Function> funcs = new LinkedHashSet<>();
        for (int t = 0; t < targets.length; t++) {
            Address ad = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(targets[t]);
            Reference[] refs = getReferencesTo(ad);
            w.printf("=== refs to %s (%08X): %d ===%n", t < names.length ? names[t] : "field", targets[t], refs.length);
            for (Reference r : refs) {
                Address from = r.getFromAddress();
                Function f = fm.getFunctionContaining(from);
                w.printf("  %s  in %s%n", from, f == null ? "(no function)" : f.getName() + " @" + f.getEntryPoint());
                if (f != null) funcs.add(f);
            }
            w.println();
        }

        // ---- 3. scan ALL instructions for a store whose operand involves 0xC139C-ish ----
        w.println("=== instructions referencing the 0xC13xx struct window (0xC1390-0xC1400) ===");
        InstructionIterator it = listing.getInstructions(true);
        int shown = 0;
        while (it.hasNext() && shown < 200) {
            Instruction ins = it.next();
            for (int op = 0; op < ins.getNumOperands(); op++) {
                Object[] objs = ins.getOpObjects(op);
                for (Object o : objs) {
                    long v = -1;
                    if (o instanceof Scalar) v = ((Scalar) o).getUnsignedValue();
                    else if (o instanceof Address) v = ((Address) o).getOffset();
                    if (v >= 0x000c1390L && v <= 0x000c1400L) {
                        Function f = fm.getFunctionContaining(ins.getAddress());
                        w.printf("  %s  %-34s in %s%n", ins.getAddress(), ins.toString(),
                                 f == null ? "?" : f.getName() + "@" + f.getEntryPoint());
                        shown++;
                    }
                }
            }
        }
        w.printf("  (%d instruction sites shown)%n%n", shown);

        // ---- 4. decompile the interesting functions ----
        w.println("=== decompiled functions touching the struct ===");
        for (Function f : funcs) {
            w.println("################################################################");
            w.printf("### %s @ %s  size=%d  callers=%d%n", f.getName(), f.getEntryPoint(),
                     f.getBody().getNumAddresses(), f.getCallingFunctions(monitor).size());
            w.println("################################################################");
            try {
                DecompileResults r = di.decompileFunction(f, 60, monitor);
                w.println(r.getDecompiledFunction().getC());
            } catch (Exception e) {
                w.println("FAILED: " + e.getMessage());
            }
            w.println();
        }

        di.dispose();
        w.close();
        println("DisMenu2: wrote " + out);
    }
}
