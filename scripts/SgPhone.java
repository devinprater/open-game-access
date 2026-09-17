// Find the PHONE MANAGER and the SELECT/CALL machinery — the choice system.
//
// ⭐ WHY: the executable contains
//      "task phone manager", "PhoneReq", "PhoneSet",
//      "Warning!! Phone not open", "Warning!! Phone not close",
//      "task select_call", "Mail buffer overflow"
//   "Phone not open/close" means the engine tracks an open/closed phone STATE — that is the
//   variable a reader must poll to know when a choice (a mail reply) is on screen.
//   This script locates the functions referencing those strings and decompiles them.
//
// @category OGA
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Data;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.util.task.ConsoleTaskMonitor;
import java.util.*;

public class SgPhone extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] needles = {
            "task phone manager", "PhoneReq", "PhoneSet",
            "Warning!! Phone not open", "Warning!! Phone not close",
            "task select_call", "Mail buffer overflow", "Select"
        };
        StringBuilder out = new StringBuilder();
        List<Address> all = new ArrayList<>();

        for (String needle : needles) {
            out.append("=== needle: ").append(needle).append(" ===\n");
            List<Address> found = new ArrayList<>();
            for (Data d : currentProgram.getListing().getDefinedData(true)) {
                try {
                    Object v = d.getValue();
                    if (v != null && v.toString().contains(needle)) found.add(d.getAddress());
                } catch (Exception e) { /* skip */ }
            }
            out.append("  string instances: ").append(found.size()).append("\n");
            for (Address a : found) {
                out.append("  ").append(a).append("\n");
                ReferenceIterator it = currentProgram.getReferenceManager().getReferencesTo(a);
                while (it.hasNext()) {
                    Reference r = it.next();
                    Function f = getFunctionContaining(r.getFromAddress());
                    if (f != null) {
                        out.append("    xref from ").append(r.getFromAddress())
                           .append(" in ").append(f.getName())
                           .append(" @").append(f.getEntryPoint()).append("\n");
                        if (!all.contains(f.getEntryPoint())) all.add(f.getEntryPoint());
                    }
                }
            }
        }

        out.append("\n=== DISTINCT FUNCTIONS (").append(all.size()).append(") ===\n");
        for (Address a : all) out.append("  ").append(a).append("\n");

        // decompile each, capped so the output stays readable
        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);
        for (Address a : all) {
            Function f = getFunctionAt(a);
            if (f == null) continue;
            out.append("\n################ FUNCTION ").append(a).append(" ################\n");
            out.append("  name=").append(f.getName()).append("  body=").append(f.getBody().getNumAddresses()).append(" bytes\n");
            DecompileResults dr = di.decompileFunction(f, 60, new ConsoleTaskMonitor());
            if (dr != null && dr.decompileCompleted() && dr.getDecompiledFunction() != null) {
                String c = dr.getDecompiledFunction().getC();
                out.append(c.length() > 4000 ? c.substring(0, 4000) + "\n... [truncated]\n" : c + "\n");
            } else {
                out.append("  (decompile failed)\n");
            }
        }

        java.io.FileWriter fw = new java.io.FileWriter("C:\\Users\\Devin Prater\\AppData\\Local\\Temp\\sg-phone-out.txt");
        fw.write(out.toString());
        fw.close();
        println("wrote " + out.length() + " chars to sg-phone-out.txt");
    }
}
