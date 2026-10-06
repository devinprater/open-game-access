// DisDelta.java -- settle the ELF offset->RAM delta, and read the ACTUAL address constant
// the menu module uses (instead of trusting Ghidra's iRam<addr> label).
//
// Two independent answers in one run:
//   1. Where does Ghidra place the string "[MENU] MESSAGE"?  (delta = live RAM - this addr)
//   2. Disassemble FUN_000e1afc and the menu module so the lui/addiu pair that builds the
//      state-struct base is visible as raw constants.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.scalar.Scalar;
import java.io.PrintWriter;

public class DisDelta extends GhidraScript {
    @Override
    public void run() throws Exception {
        PrintWriter w = new PrintWriter(getScriptArgs().length > 0 ? getScriptArgs()[0] : "delta.txt");
        Memory mem = currentProgram.getMemory();
        Listing listing = currentProgram.getListing();

        // ---- 1. locate the strings by BYTES (never by a precomputed address) ----
        w.println("=== string locations in Ghidra ===");
        for (String s : new String[]{"[MENU] MESSAGE", "[MENU] UPDATE", "[TITLE] UPDATE", "%05ddmg"}) {
            byte[] needle = s.getBytes("US-ASCII");
            Address cur = mem.getMinAddress();
            int n = 0;
            while (n < 3) {
                Address found = mem.findBytes(cur, mem.getMaxAddress(), needle, null, true, monitor);
                if (found == null) break;
                w.printf("  %-16s @ ghidra %s%n", s, found);
                n++;
                cur = found.add(1);
            }
            if (n == 0) w.printf("  %-16s NOT FOUND%n", s);
        }
        w.println();

        // ---- 2. disassemble the menu module head: raw constants tell the real address ----
        for (long f : new long[]{0x000e1afcL, 0x000e2604L, 0x000e2374L}) {
            w.printf("=== disassembly of FUN_%07x (first 34 instructions) ===%n", f);
            Address a = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(f);
            Instruction ins = listing.getInstructionAt(a);
            int n = 0;
            while (ins != null && n < 34) {
                StringBuilder ops = new StringBuilder();
                for (int op = 0; op < ins.getNumOperands(); op++) {
                    for (Object o : ins.getOpObjects(op)) {
                        if (o instanceof Scalar) ops.append(String.format("0x%X ", ((Scalar) o).getUnsignedValue()));
                        else if (o instanceof Address) ops.append(o + " ");
                    }
                }
                w.printf("  %s  %-38s %s%n", ins.getAddress(), ins.toString(), ops.toString().trim());
                ins = ins.getNext();
                n++;
            }
            w.println();
        }

        // ---- 3. every distinct immediate in the 0xC1300-0xC1400 window anywhere ----
        w.println("=== immediates in 0xC0000-0xD0000 with the functions using them ===");
        InstructionIterator it = listing.getInstructions(true);
        int shown = 0;
        while (it.hasNext() && shown < 80) {
            Instruction i2 = it.next();
            for (int op = 0; op < i2.getNumOperands(); op++) {
                for (Object o : i2.getOpObjects(op)) {
                    if (o instanceof Scalar) {
                        long v = ((Scalar) o).getUnsignedValue();
                        if (v >= 0xC0000L && v < 0xD0000L) {
                            Function fn = currentProgram.getFunctionManager().getFunctionContaining(i2.getAddress());
                            w.printf("  %s  %-30s %s%n", i2.getAddress(), i2.toString(),
                                     fn == null ? "?" : fn.getName() + "@" + fn.getEntryPoint());
                            shown++;
                        }
                    }
                }
            }
        }
        w.close();
        println("DisDelta: wrote");
    }
}
