// TagTeamQuery.java — locate the state the accessibility adapter needs.
//
// WHAT THIS GAME IS: DBZ: Tenkaichi Tag Team (ULUS10537), a 3D arena fighter. For a
// screen-reader adapter the valuable state is the BATTLE HUD and character stats:
//   - which characters are on each team (names, for the roster readout)
//   - health / ki per fighter (the resource bars)
//   - the camera/player position (navigation, as with Another Road's beacon)
//   - menu cursors (character select, main menu)
//
// The ELF here is PRE-LINKED to absolute addresses — program headers give
// vaddr 0x08804040 and 0x08AEFB70 — so RAM addresses in the binary are literal and can be
// searched for directly. That is the opposite of the Another Road case (vaddr 0), where a
// constant base had to be derived.
//
// This script: lists engine strings that name the systems, finds any absolute code/data
// addresses in instruction operands, and decompiles functions referencing the strings.
//
// @category OGA
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.scalar.Scalar;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.util.task.ConsoleTaskMonitor;
import java.util.*;

public class TagTeamQuery extends GhidraScript {

    void dumpFn(StringBuilder out, DecompInterface di, Address a, int cap) {
        Function f = getFunctionAt(a);
        if (f == null) return;
        out.append("\n################ FUNCTION ").append(a).append(" ################\n");
        out.append("  name=").append(f.getName())
           .append("  body=").append(f.getBody().getNumAddresses()).append(" bytes\n");
        try {
            DecompileResults dr = di.decompileFunction(f, 60, new ConsoleTaskMonitor());
            if (dr != null && dr.decompileCompleted() && dr.getDecompiledFunction() != null) {
                String c = dr.getDecompiledFunction().getC();
                out.append(c.length() > cap ? c.substring(0, cap) + "\n... [truncated]\n" : c + "\n");
            } else out.append("  (decompile failed)\n");
        } catch (Exception e) { out.append("  (error: ").append(e.getMessage()).append(")\n"); }
    }

    @Override
    public void run() throws Exception {
        StringBuilder out = new StringBuilder();

        // ---- 1. engine strings naming plausible systems ----
        // UTF-16LE is common in this era; Ghidra's string search may surface either, and
        // the Another Road lesson was that an ASCII-only scan silently misses UTF-16 text.
        String[] needles = {
            "hp", "HP", "health", "Health", "life", "Life", "damage", "Damage",
            "ki", "KI", "gauge", "Gauge", "battle", "Battle", "BATTLE",
            "char", "Char", "player", "Player", "team", "Team",
            "camera", "Camera", "pos", "Pos", "position",
            "select", "Select", "cursor", "Cursor",
            "packfile", "PACKFILE", "pak", "PAK", "archive",
            "menu", "Menu", "window", "Window", "text", "Text"
        };
        Map<Address, String> strings = new LinkedHashMap<>();
        for (Data d : currentProgram.getListing().getDefinedData(true)) {
            try {
                Object v = d.getValue();
                if (v == null) continue;
                String s = v.toString();
                if (s.length() < 3 || s.length() > 60) continue;
                for (String nd : needles) {
                    if (s.contains(nd)) { strings.put(d.getAddress(), s); break; }
                }
            } catch (Exception e) { /* skip */ }
        }

        out.append("=== engine strings matching adapter keywords: ").append(strings.size()).append(" ===\n");
        Set<Address> funcs = new LinkedHashSet<>();
        int shown = 0;
        for (Map.Entry<Address, String> e : strings.entrySet()) {
            if (shown++ < 250) out.append("  ").append(e.getKey()).append("  ").append(e.getValue()).append("\n");
            ReferenceIterator it = currentProgram.getReferenceManager().getReferencesTo(e.getKey());
            while (it.hasNext()) {
                Reference r = it.next();
                Function f = getFunctionContaining(r.getFromAddress());
                if (f != null) funcs.add(f.getEntryPoint());
            }
        }
        out.append("\n=== functions referencing them: ").append(funcs.size()).append(" ===\n");
        for (Address a : funcs) out.append("  ").append(a).append("\n");

        // ---- 2. absolute RAM addresses as instruction operands ----
        // The ELF is pre-linked, so a global access often appears as a literal in the
        // 0x08800000..0x0A000000 range. Collect them with counts: a heavily-referenced
        // global is a good candidate for a real data structure rather than a scratch value.
        Map<Long, Integer> absl = new HashMap<>();
        for (Instruction ins : currentProgram.getListing().getInstructions(true)) {
            for (int i = 0; i < ins.getNumOperands(); i++) {
                for (Object o : ins.getOpObjects(i)) {
                    if (o instanceof Scalar) {
                        long v = ((Scalar) o).getUnsignedValue();
                        if (v >= 0x08800000L && v < 0x0A000000L) {
                            absl.merge(v, 1, Integer::sum);
                        }
                    }
                }
            }
        }
        out.append("\n=== absolute RAM literals in code: ").append(absl.size()).append(" distinct ===\n");
        List<Map.Entry<Long, Integer>> top = new ArrayList<>(absl.entrySet());
        top.sort((a, b) -> b.getValue() - a.getValue());
        for (int i = 0; i < Math.min(top.size(), 80); i++)
            out.append(String.format("  0x%08X  x%d%n", top.get(i).getKey(), top.get(i).getValue()));

        // ---- 3. decompile a bounded sample of the string-referencing functions ----
        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);
        out.append("\n=== decompiling up to 40 of ").append(funcs.size())
           .append(" string-referencing functions ===\n");
        int n = 0;
        for (Address a : funcs) {
            if (n++ >= 40) break;
            dumpFn(out, di, a, 3000);
        }

        java.io.FileWriter fw = new java.io.FileWriter(
            "C:\\Users\\Devin Prater\\AppData\\Local\\Temp\\tagteam-query-out.txt");
        fw.write(out.toString());
        fw.close();
        println("wrote " + out.length() + " chars to tagteam-query-out.txt");
    }
}
