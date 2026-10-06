// DisMsgApi.java -- the message LOOKUP API (the place to hook).
//
// The message container handle is uRam0009f52c. These functions all read it, so one of them is
// the "give me the text for this id" routine -- and hooking THAT gives the displayed line with
// no need to find any line index.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.app.decompiler.*;
import java.io.PrintWriter;
import java.util.*;

public class DisMsgApi extends GhidraScript {
    @Override
    public void run() throws Exception {
        PrintWriter w = new PrintWriter(getScriptArgs().length > 0 ? getScriptArgs()[0] : "msgapi.txt");
        FunctionManager fm = currentProgram.getFunctionManager();
        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);

        long[] targets = new long[]{0xf6374L, 0xfa8b4L, 0x10e908L, 0xe62fcL, 0x11f104L, 0x11f2fcL,
                                    0x11f3f4L, 0x11f4d8L, 0x6709cL, 0xd5a60L, 0xd5e34L, 0xfef44L,
                                    0xd9bdcL, 0x136de0L};
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
        di.dispose();
        w.close();
        println("DisMsgApi: wrote");
    }
}
