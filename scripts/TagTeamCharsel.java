// TagTeamCharsel.java — find the CHARACTER SELECT renderer and its selection index.
//
// WHY: RAM sweeping for the character-select cursor failed. Three stepped dumps showed
// ~73-88 KB changing per press (mostly redraw), a strict +1 search returned nothing, and the
// first-changed address held a countdown. That is the same dead end Another Road hit on its
// story index, and the reliable route is the DECOMPILER: find the function that draws the
// character name plate, then read the variable it indexes with.
//
// TARGETS: the UI element names already recovered from this binary —
//   charaname, icon_chara, chara_name_01, chara_name_02, menu_01..03,
//   30_select_ok_%d, 31_player_%02d, text_playername
//
// The character select draws per-player panels ("31_player_%02d") each with a name plate
// ("charaname" / "chara_name_0N"), so the renderer takes a player/character index as an
// argument. Decompiling it exposes both the index and the table it indexes.
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

public class TagTeamCharsel extends GhidraScript {

    void dump(StringBuilder out, DecompInterface di, Address a, int cap) {
        Function f = getFunctionAt(a);
        if (f == null) return;
        out.append("\n################ FUNCTION ").append(a).append(" ################\n");
        out.append("  name=").append(f.getName())
           .append("  body=").append(f.getBody().getNumAddresses()).append(" bytes")
           .append("  params=").append(f.getParameterCount()).append("\n");
        try {
            DecompileResults dr = di.decompileFunction(f, 90, new ConsoleTaskMonitor());
            if (dr != null && dr.decompileCompleted() && dr.getDecompiledFunction() != null) {
                String c = dr.getDecompiledFunction().getC();
                out.append(c.length() > cap ? c.substring(0, cap) + "\n... [truncated]\n" : c + "\n");
            } else out.append("  (decompile failed)\n");
        } catch (Exception e) { out.append("  (error: ").append(e.getMessage()).append(")\n"); }
    }

    @Override
    public void run() throws Exception {
        StringBuilder out = new StringBuilder();
        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);

        // character-select-specific element names
        String[] needles = {
            "charaname", "icon_chara", "chara_name_01", "chara_name_02",
            "30_select_ok_", "31_player_", "40_stage_select_cursor", "40_stage_select_tab",
            "text_playername", "icon_team", "menu%02d", "menu_%02d", "menu_01", "menu_02", "menu_03"
        };

        Map<Address, String> stringAddrs = new LinkedHashMap<>();
        for (Data d : currentProgram.getListing().getDefinedData(true)) {
            try {
                Object v = d.getValue();
                if (v == null) continue;
                String s = v.toString();
                for (String nd : needles) if (s.contains(nd)) { stringAddrs.put(d.getAddress(), s); break; }
            } catch (Exception e) { /* skip */ }
        }

        out.append("=== character-select element strings: ").append(stringAddrs.size()).append(" ===\n");
        Set<Address> funcs = new LinkedHashSet<>();
        for (Map.Entry<Address, String> e : stringAddrs.entrySet()) {
            out.append("  ").append(e.getKey()).append("  ").append(e.getValue()).append("\n");
            ReferenceIterator it = currentProgram.getReferenceManager().getReferencesTo(e.getKey());
            while (it.hasNext()) {
                Reference r = it.next();
                Function f = getFunctionContaining(r.getFromAddress());
                if (f != null) funcs.add(f.getEntryPoint());
            }
        }

        out.append("\n=== functions referencing them (").append(funcs.size()).append(") ===\n");
        for (Address a : funcs) {
            Function f = getFunctionAt(a);
            out.append("  ").append(a)
               .append("  params=").append(f == null ? "?" : f.getParameterCount())
               .append("  body=").append(f == null ? "?" : f.getBody().getNumAddresses()).append("\n");
        }

        out.append("\n=== DECOMPILED ===\n");
        for (Address a : funcs) dump(out, di, a, 7000);

        java.io.FileWriter fw = new java.io.FileWriter(
            "C:\\Users\\Devin Prater\\AppData\\Local\\Temp\\tagteam-charsel-out.txt");
        fw.write(out.toString());
        fw.close();
        println("wrote " + out.length() + " chars to tagteam-charsel-out.txt");
    }
}
