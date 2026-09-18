// DisFindInput.java -- find the controller-input path, then the code that CONSUMES buttons.
//
// WHY THIS IS THE RIGHT NEXT MOVE (doc section 39)
//   Thirty-nine sections have cleared every structure reachable from the menu manager: the engine
//   handles, the chapter table, the manager header (static over 14 presses), its 35-node list (static),
//   a 0.25 MB region with no wrapping field, and three registered handlers that turned out to be draw
//   callbacks. The highlight is not in any of them.
//
//   What has never been examined is the CODE PATH: something must READ the current highlight in order
//   to draw it, and something must WRITE it in response to a d-pad press. That code is reachable from
//   the controller import stubs.
//
//   This EBOOT has a full section-header table, so the stubs are named:
//       .sceStub.text.sceCtrl      vaddr 0x0036DA44  size 0x18
//       .sceStub.text.sceDisplay   vaddr 0x0036DA14
//   Callers of the sceCtrl stub are the input poll. The functions that then act on the polled button
//   bits are the navigation code, and THAT is where a selection index is read and written.
//
// WHAT IT EMITS
//   For each target: the function, its size, its callers, and the decompilation. Also a scan of all
//   defined functions for any that reference the stub addresses, so the input poll is found even if
//   Ghidra did not create a direct CALL reference.
//
// USAGE
//   analyzeHeadless <projDir> DISSIDIA_ELF -process EBOOT.dec -noanalysis
//       -scriptPath "<projDir>" -postScript DisFindInput.java
//
// @category OGA

import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.symbol.Reference;

import java.io.File;
import java.io.PrintWriter;
import java.util.LinkedHashSet;
import java.util.Set;

public class DisFindInput extends GhidraScript {

    // the named import stubs, from the section header table
    private static final long[] STUBS = {
        0x0036DA44L,   // .sceStub.text.sceCtrl
        0x0036DA14L,   // .sceStub.text.sceDisplay
        0x0036DA5CL,   // .sceStub.text.sceSuspendForUser
    };

    private static final int MAX_CHARS = 30000;

    @Override
    public void run() throws Exception {
        String outPath = System.getProperty("user.home")
                + "/oga-ghidra-dissidia/input-report.txt";
        PrintWriter out = new PrintWriter(new File(outPath), "UTF-8");

        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);

        // ---- phase 1: who references the sceCtrl stub? ----
        Set<Function> callers = new LinkedHashSet<>();
        out.println("################################################################");
        out.println("### REFERENCES TO THE IMPORT STUBS");
        out.println("################################################################");
        for (long s : STUBS) {
            Address a = toAddr(s);
            out.println("-- stub 0x" + Long.toHexString(s) + " --");
            int n = 0;
            for (Reference r : currentProgram.getReferenceManager().getReferencesTo(a)) {
                Address from = r.getFromAddress();
                Function f = getFunctionContaining(from);
                out.println("   " + r.getReferenceType() + " from " + from
                            + (f != null ? "   in " + f.getName() + " @" + f.getEntryPoint() : ""));
                if (f != null) {
                    callers.add(f);
                }
                if (++n >= 60) { out.println("   ..."); break; }
            }
            if (n == 0) {
                out.println("   (no references recorded -- Ghidra may not have resolved the stub)");
            }
        }

        // ---- phase 2: also scan bodies for the stub addresses, in case references are absent ----
        out.println();
        out.println("################################################################");
        out.println("### FUNCTIONS WHOSE BODY MENTIONS A STUB ADDRESS");
        out.println("################################################################");
        AddressSet stubSet = new AddressSet();
        for (long s : STUBS) {
            stubSet.add(toAddr(s), toAddr(s + 0x10));
        }
        int scanned = 0, mentioned = 0;
        FunctionIterator fit = currentProgram.getFunctionManager().getFunctions(true);
        while (fit.hasNext()) {
            Function f = fit.next();
            scanned++;
            if (f.getBody().intersects(stubSet)) {
                continue;
            }
            boolean hit = false;
            for (Reference r : currentProgram.getReferenceManager().getReferencesTo(f.getEntryPoint())) {
                // no-op: kept for symmetry
            }
            // walk instructions for a reference into the stub range
            ghidra.program.model.listing.InstructionIterator iit =
                    currentProgram.getListing().getInstructions(f.getBody(), true);
            while (iit.hasNext()) {
                ghidra.program.model.listing.Instruction ins = iit.next();
                for (Reference r : ins.getReferencesFrom()) {
                    Address to = r.getToAddress();
                    if (to != null && stubSet.contains(to)) {
                        hit = true;
                        break;
                    }
                }
                if (hit) break;
            }
            if (hit) {
                mentioned++;
                callers.add(f);
            }
        }
        out.println("scanned " + scanned + " functions; " + mentioned + " mention a stub address");

        // ---- phase 3: decompile the input-path candidates ----
        out.println();
        out.println("################################################################");
        out.println("### DECOMPILED INPUT-PATH CANDIDATES (" + callers.size() + ")");
        out.println("################################################################");
        int done = 0;
        for (Function f : callers) {
            if (done++ >= 12) {
                out.println("(truncated at 12 functions)");
                break;
            }
            out.println();
            out.println("=================================================================");
            out.println("=== " + f.getName() + " @" + f.getEntryPoint()
                        + "  size=" + f.getBody().getNumAddresses());
            out.println("=================================================================");
            try {
                DecompileResults dr = di.decompileFunction(f, 180, monitor);
                if (dr != null && dr.decompileCompleted()) {
                    String txt = dr.getDecompiledFunction().getC();
                    if (txt != null && txt.length() > MAX_CHARS) {
                        txt = txt.substring(0, MAX_CHARS) + "\n/* ...truncated... */";
                    }
                    out.println(txt);
                } else {
                    out.println("(decompile did not complete)");
                }
            } catch (Exception e) {
                out.println("(decompile threw: " + e + ")");
            }
        }

        out.flush();
        out.close();
        di.dispose();
        println("DisFindInput -> " + outPath);
    }
}
