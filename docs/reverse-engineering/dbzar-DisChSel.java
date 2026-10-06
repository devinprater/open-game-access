// DisChSel.java -- JOB A final: the chapter-record processor.
//
// FUN_00031484 returns DAT_001b0b18 + idx*0x140 -- i.e. chapters have 320-byte records.
// FUN_00008590(chapterRecord, 0x99DAE-record, idx, DAT_001b11d6) processes them. If the
// Chapter Select browse cursor lives in one of those records, this decompile shows where.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.Memory;
import ghidra.app.decompiler.*;
import java.io.PrintWriter;

public class DisChSel extends GhidraScript {
    @Override
    public void run() throws Exception {
        PrintWriter w = new PrintWriter(getScriptArgs().length > 0 ? getScriptArgs()[0] : "chsel.txt");
        FunctionManager fm = currentProgram.getFunctionManager();
        Memory mem = currentProgram.getMemory();
        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);

        w.println("=== the chapter record base DAT_001b0b18 (static) ===");
        try {
            Address a = currentProgram.getImageBase().add(0x1B0B18L);
            byte[] buf = new byte[0x140];
            mem.getBytes(a, buf);
            for (int i = 0; i < buf.length; i += 16) {
                StringBuilder hex = new StringBuilder(), asc = new StringBuilder();
                for (int j = 0; j < 16 && i + j < buf.length; j++) {
                    hex.append(String.format("%02x ", buf[i + j]));
                    int c = buf[i + j] & 0xff;
                    asc.append(c >= 0x20 && c < 0x7f ? (char) c : '.');
                }
                w.printf("  0x%06X  %-48s %s%n", 0x1B0B18L + i, hex.toString(), asc.toString());
            }
        } catch (Exception e) { w.println("  static dump failed: " + e.getMessage()); }

        long[] targets = new long[]{0x8590L, 0x314a8L, 0x31484L, 0x31848L, 0x31c2cL, 0x31c88L,
                                    0x7cc0L, 0x96ecL, 0x9c8cL, 0xa1c0L, 0xa0a8L, 0x9744L, 0x9774L,
                                    0x85e8L, 0xb550L, 0xb924L, 0xb5e8L, 0x89d4L, 0x85e8L};
        java.util.Set<Long> done = new java.util.LinkedHashSet<>();
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
        di.dispose();
        w.close();
        println("DisChSel: wrote");
    }
}
