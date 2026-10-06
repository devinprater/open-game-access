// DisBlocks.java -- settle whether 0xC139C is writable data or code, and locate the real
// menu state, because the decompile's iRam000c139c can be a Ghidra naming artifact.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.listing.*;
import ghidra.program.model.scalar.Scalar;
import java.io.PrintWriter;
import java.util.*;

public class DisBlocks extends GhidraScript {
    @Override
    public void run() throws Exception {
        PrintWriter w = new PrintWriter(getScriptArgs().length > 0 ? getScriptArgs()[0] : "blocks.txt");

        w.println("=== memory blocks ===");
        for (MemoryBlock b : currentProgram.getMemory().getBlocks()) {
            w.printf("  %-22s %s - %s  size=0x%X  R=%b W=%b X=%b  init=%b%n",
                b.getName(), b.getStart(), b.getEnd(), b.getSize(),
                b.isRead(), b.isWrite(), b.isExecute(), b.isInitialized());
        }
        w.println();

        // where does 0xC139C land?
        long[] probes = { 0x000c139cL, 0x000c140cL, 0x000c1410L, 0x00034660L, 0x000e7ba0L, 0x001e7ba0L };
        w.println("=== probe addresses: block + raw bytes ===");
        for (long p : probes) {
            Address a = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(p);
            MemoryBlock b = currentProgram.getMemory().getBlock(a);
            StringBuilder sb = new StringBuilder();
            for (int i = 0; i < 16 && currentProgram.getMemory().contains(a.add(i)); i++) {
                sb.append(String.format("%02X ", currentProgram.getMemory().getByte(a.add(i)) & 0xff));
            }
            w.printf("  %08X  block=%-20s  bytes=%s%n", p, b == null ? "(none)" : b.getName(), sb);
        }
        w.println();

        // every defined pointer / scalar whose value is 0xC139C or nearby
        w.println("=== references to 0xC139C and where they come from ===");
        Address target = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(0x000c139cL);
        for (ghidra.program.model.symbol.Reference r : getReferencesTo(target)) {
            Address from = r.getFromAddress();
            Function f = currentProgram.getFunctionManager().getFunctionContaining(from);
            w.printf("  from %s  type=%s  in %s%n", from, r.getReferenceType(),
                     f == null ? "?" : f.getName() + "@" + f.getEntryPoint());
        }
        w.println();

        // does the file text at 0xC139C differ from a plausible BSS address? report the
        // menu-module function offsets so the caller can cross-check with live RAM.
        w.println("=== menu module functions (for live cross-check) ===");
        long[] fns = { 0x000e1afcL, 0x000e2374L, 0x000e2468L, 0x000e2604L, 0x000e28b8L };
        for (long f : fns) {
            Address a = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(f);
            Function fn = currentProgram.getFunctionManager().getFunctionAt(a);
            w.printf("  %08X  %s%n", f, fn == null ? "(none)" : fn.getName());
        }
        w.close();
        println("DisBlocks: wrote");
    }
}
