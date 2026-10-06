// DisCommon.java -- decompile the shared handler helpers, to find the text path (the
// MSG-ID -> string resolver) that screen titles come from.
//
// From DisHandlers.java the common callees are:
//   FUN_000e1d70 (97)  FUN_000e1de4 (68)  FUN_000e1df4 (22)
//   FUN_000e316c (22)  FUN_000e314c (15)  FUN_000e312c (12)
// plus handler FUN_000da4d4, which carries 9 immediate constants in message-id range.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.app.decompiler.*;
import java.io.PrintWriter;

public class DisCommon extends GhidraScript {
    @Override
    public void run() throws Exception {
        PrintWriter w = new PrintWriter(getScriptArgs().length > 0 ? getScriptArgs()[0] : "common.txt");
        FunctionManager fm = currentProgram.getFunctionManager();
        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);

        long[] fns = {
            0x000e1d70L, 0x000e1de4L, 0x000e1df4L,
            0x000e316cL, 0x000e314cL, 0x000e312cL,
            0x000da4d4L, 0x000e62fcL,
        };
        for (long a : fns) {
            Address ad = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(a);
            Function f = fm.getFunctionAt(ad);
            w.println("################################################################");
            if (f == null) { w.println("### NO FUNCTION AT " + ad); continue; }
            w.printf("### %s @ %s  size=%d  callers=%d%n", f.getName(), ad,
                     f.getBody().getNumAddresses(), f.getCallingFunctions(monitor).size());
            w.println("### callers: " + f.getCallingFunctions(monitor));
            w.println("################################################################");
            try { w.println(di.decompileFunction(f, 60, monitor).getDecompiledFunction().getC()); }
            catch (Exception e) { w.println("FAILED: " + e.getMessage()); }
            w.println();
        }
        di.dispose();
        w.close();
        println("DisCommon: wrote");
    }
}
