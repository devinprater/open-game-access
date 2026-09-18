// Search for callers of the probable message/text resolver and print a small
// backward p-code slice for each argument.
//
// Run in the Code Browser with Script Manager, or headlessly:
//   analyzeHeadless <project-dir> <project-name> -process <program> \
//     -scriptPath <repo>\scripts -postScript TagTeamNameResolver.java

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.pcode.HighFunction;
import ghidra.program.model.pcode.PcodeOp;
import ghidra.program.model.pcode.PcodeOpAST;
import ghidra.program.model.pcode.Varnode;
import ghidra.util.task.ConsoleTaskMonitor;

import java.util.HashSet;
import java.util.Iterator;
import java.util.Set;

public class TagTeamNameResolver extends GhidraScript {
    private static final String RESOLVER = "0883ee74";
    private static final int MAX_DEPTH = 10;

    // Collect everything so the run is VERIFIABLE from a written artefact, not just stdout.
    // (A log line is not evidence; only a file is.)
    private final StringBuilder LOG = new StringBuilder();
    private void log(String s) { LOG.append(s).append('\n'); }

    @Override
    public void run() throws Exception {
        Address targetAddress = toAddr(RESOLVER);
        Function target = getFunctionAt(targetAddress);
        if (target == null) {
            printerr("No function at " + targetAddress +
                ". Check that the pre-linked ELF is loaded at its PSP addresses.");
            return;
        }

        log("Target: " + target.getName() + " @ " + target.getEntryPoint());
        log("CALL input 0 is the destination; input 1 is the first MIPS argument.");

        DecompInterface decompiler = new DecompInterface();
        decompiler.toggleCCode(true);
        decompiler.toggleSyntaxTree(true);
        if (!decompiler.openProgram(currentProgram)) {
            printerr("Decompiler initialization failed: " + decompiler.getLastMessage());
            return;
        }

        int hits = 0;
        for (Function caller : currentProgram.getFunctionManager().getFunctions(true)) {
            if (monitor.isCancelled()) break;
            DecompileResults result = decompiler.decompileFunction(
                caller, 60, new ConsoleTaskMonitor());
            HighFunction high = result.getHighFunction();
            if (high == null) continue;

            Iterator<PcodeOpAST> ops = high.getPcodeOps();
            while (ops.hasNext()) {
                PcodeOpAST op = ops.next();
                if (op.getOpcode() != PcodeOp.CALL || op.getNumInputs() < 2) continue;
                Varnode destination = op.getInput(0);
                if (!destination.isAddress() ||
                    !destination.getAddress().equals(targetAddress)) continue;

                hits++;
                log("");
                log("=== " + caller.getName() + " @ " + caller.getEntryPoint() +
                    " callsite " + op.getSeqnum().getTarget() + " ===");
                for (int i = 1; i < op.getNumInputs(); i++) {
                    log("arg" + (i - 1) + ": " + op.getInput(i));
                    dumpSlice(op.getInput(i), "  ", 0, new HashSet<Varnode>());
                }
            }
        }

        log("");
        log("Resolver callsites found: " + hits);
        log("Look for a caller in the battle/HUD path whose arg0 slice contains " +
            "LOAD/PTRADD from a slot-indexed object or a separate roster record.");

        String path = System.getProperty("user.dir") + java.io.File.separator + "tagteam-resolver-out.txt";
        try (java.io.PrintWriter pw = new java.io.PrintWriter(new java.io.FileWriter(path))) {
            pw.print(LOG);
        }
        println("TagTeamNameResolver.java> wrote " + LOG.length() + " chars to tagteam-resolver-out.txt");
    }

    private void dumpSlice(Varnode node, String indent, int depth, Set<Varnode> seen) {
        if (node == null || depth >= MAX_DEPTH || !seen.add(node)) return;
        PcodeOp definition = node.getDef();
        if (definition == null) {
            log(indent + leaf(node));
            return;
        }

        log(indent + definition.toString());
        for (int i = 0; i < definition.getNumInputs(); i++) {
            Varnode input = definition.getInput(i);
            if (input != null && !input.isConstant()) {
                dumpSlice(input, indent + "  ", depth + 1, seen);
            } else if (input != null) {
                log(indent + "  const " + input);
            }
        }
    }

    private String leaf(Varnode node) {
        if (node.isConstant()) return "const " + node;
        if (node.isAddress()) return "address " + node.getAddress();
        if (node.isRegister()) return "register " + node;
        return "leaf " + node;
    }
}
