// DisMenu.java -- decompile the DBZ-AR menu module cluster and dump, so the selection
// mechanism can be read rather than guessed.
//
//   analyzeHeadless <proj> DBZAR -process EBOOT.dec -noanalysis \
//     -scriptPath <dir> -postScript DisMenu.java <outfile>
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.app.decompiler.*;
import java.io.PrintWriter;

public class DisMenu extends GhidraScript {
    @Override
    public void run() throws Exception {
        String out = getScriptArgs().length > 0 ? getScriptArgs()[0] : "menu-decomp.txt";
        PrintWriter w = new PrintWriter(out);
        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);
        FunctionManager fm = currentProgram.getFunctionManager();

        // the cluster the earlier session named, plus the whole 0x000E1xxx-0x000E2xxx range
        long[] addrs = {
            0x000e1afcL, 0x000e2374L, 0x000e2468L, 0x000e28b8L,
            0x000e2604L, 0x000e2524L, 0x000e3c88L, 0x000e2ea8L,
            0x000ebbb8L, 0x000ebec8L, 0x000ebfecL,
            0x00120d40L, 0x00120ffcL, 0x00121698L,
        };
        for (long a : addrs) {
            Address ad = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(a);
            Function f = fm.getFunctionAt(ad);
            w.println("################################################################");
            if (f == null) { w.println("### NO FUNCTION AT " + ad); w.println(); continue; }
            w.printf("### %s @ %s  size=%d  callers=%d%n", f.getName(), ad,
                     f.getBody().getNumAddresses(), f.getCallingFunctions(monitor).size());
            w.println("################################################################");
            try {
                DecompileResults r = di.decompileFunction(f, 60, monitor);
                w.println(r.getDecompiledFunction().getC());
            } catch (Exception e) {
                w.println("DECOMPILE FAILED: " + e.getMessage());
            }
            w.println();
        }
        di.dispose();
        w.close();
        println("DisMenu: wrote " + out);
    }
}
