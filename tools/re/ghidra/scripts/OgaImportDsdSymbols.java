// OgaImportDsdSymbols.java — apply names from a ds-decomp `symbols.txt` to a Ghidra
// program.
//
// This is the symbol-interchange path: names discovered by the community decompilation
// flow INTO Ghidra, so a query like "who reads Force.id" lands on real function names
// instead of 3,774 anonymous FUN_xxxxxxxx entries.
//
// ⛔ IT ONLY APPLIES REAL NAMES. ds-decomp symbols.txt contains thousands of placeholder
// entries (func_02000abc). Applying those to Ghidra's own FUN_02000abc equivalents would
// churn the database and add zero information, so placeholders are counted and skipped.
// The interesting number is how many GENUINE names the decomp gives us.
//
// ⛔ IT NEVER GUESSES. A name in the decomp's symbols.txt is evidence that someone
// identified that address; the script copies it as-is. It does not rename anything that
// is not in the file, and it reports conflicts rather than silently overwriting a name
// that is already there.
//
// ds-decomp symbols.txt line format:
//   <name> kind:function(thumb,size=0x4) addr:0x0200007e
//   <name> kind:data addr:0x02197254
//   <name> kind:bss addr:0x021974d8
//
//@category OGA

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.SourceType;
import ghidra.program.model.symbol.SymbolTable;

import java.io.BufferedReader;
import java.io.FileReader;
import java.util.ArrayList;
import java.util.List;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

public class OgaImportDsdSymbols extends GhidraScript {

    private static final Pattern P = Pattern.compile(
            "^(\\S+)\\s+kind:(\\w+)(?:\\(([^)]*)\\))?\\s+addr:0x([0-9a-fA-F]+)\\s*$");

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 1) {
            println("usage: OgaImportDsdSymbols.java <symbols.txt> [--include-placeholders]");
            return;
        }
        String path = args[0];
        boolean includePlaceholders = false;
        for (String a : args) {
            if (a.equals("--include-placeholders")) includePlaceholders = true;
        }

        List<String[]> entries = new ArrayList<>();   // {name, kind, addrHex, extra}
        int placeholders = 0, malformed = 0;

        try (BufferedReader r = new BufferedReader(new FileReader(path))) {
            String line;
            while ((line = r.readLine()) != null) {
                if (monitor.isCancelled()) break;
                line = line.trim();
                if (line.isEmpty() || line.startsWith("#")) continue;

                Matcher m = P.matcher(line);
                if (!m.matches()) { malformed++; continue; }

                String name = m.group(1);
                String kind = m.group(2);
                String extra = m.group(3) == null ? "" : m.group(3);
                String addr = m.group(4);

                boolean isPlaceholder = name.startsWith("func_") || name.startsWith("FUN_")
                                     || name.startsWith("unk_") || name.startsWith("data_")
                                     || name.startsWith("lbl_");
                if (isPlaceholder && !includePlaceholders) { placeholders++; continue; }

                entries.add(new String[]{name, kind, addr, extra});
            }
        }

        SymbolTable st = currentProgram.getSymbolTable();
        int applied = 0, functionsNamed = 0, dataNamed = 0, conflicts = 0, outside = 0, failed = 0;

        for (String[] e : entries) {
            if (monitor.isCancelled()) break;
            String name = e[0], kind = e[1];
            long addrVal;
            try {
                addrVal = Long.parseUnsignedLong(e[2], 16);
            } catch (NumberFormatException ex) { continue; }

            Address a = toAddr(addrVal);
            if (a == null) { outside++; continue; }
            // Only touch addresses that exist in this program: an overlay's symbols must
            // not be applied to the ARM9 binary, even though both are in the same file set.
            if (!currentProgram.getMemory().contains(a)) { outside++; continue; }

            try {
                if (kind.equals("function")) {
                    Function f = getFunctionAt(a);
                    if (f == null) {
                        // Respect the decomp's thumb annotation when creating the function.
                        boolean thumb = e[3] != null && e[3].contains("thumb");
                        if (thumb && currentProgram.getLanguage()
                                .getLanguageDescription().getInstructionEndian() != null) {
                            // leave CPU state alone; Ghidra detects ARM/Thumb from instructions
                        }
                        f = createFunction(a, name);
                        if (f == null) continue;
                        applied++; functionsNamed++;
                    } else {
                        if (!f.getName().startsWith("FUN_")) { conflicts++; continue; }
                        f.setName(name, SourceType.IMPORTED);
                        applied++; functionsNamed++;
                    }
                } else {
                    // data / bss / rodata — create or RENAME the label.
                    //
                    // ⛔ createLabel() FAILS SILENTLY when the address already has a
                    // primary symbol, and Ghidra auto-creates DAT_/LAB_/UNK_ labels
                    // during analysis. So a bss global like gForces was silently dropped
                    // (leaving DAT_021974dc) while others imported fine — the import
                    // reported success because the exception was swallowed.
                    // Rename the existing auto-created symbol instead.
                    var existing = st.getPrimarySymbol(a);
                    if (existing != null) {
                        String en = existing.getName();
                        boolean auto = en.startsWith("DAT_") || en.startsWith("LAB_")
                                    || en.startsWith("UNK_") || en.startsWith("SUB_")
                                    || en.startsWith("FUN_");
                        if (!auto) { conflicts++; continue; }
                        existing.setName(name, SourceType.IMPORTED);
                        applied++; dataNamed++;
                    } else {
                        st.createLabel(a, name, SourceType.IMPORTED);
                        applied++; dataNamed++;
                    }
                }
            } catch (Exception ex) {
                // ⛔ Do NOT swallow this silently. An earlier version did, which is why
                // gForces (a bss global whose address already carried an auto-created
                // DAT_ label) was dropped while the import still reported success. Count
                // and report failures so a partial import is visible.
                failed++;
                if (failed <= 5) println("  !! entry failed: " + name + " @" + e[2] + " : " + ex);
            }
        }

        println("{");
        println("  \"file\": " + q(path) + ",");
        println("  \"parsed\":" + (entries.size() + placeholders) + ",");
        println("  \"skippedPlaceholders\":" + placeholders + ",");
        println("  \"malformed\":" + malformed + ",");
        println("  \"applied\":" + applied + ",");
        println("  \"functionsNamed\":" + functionsNamed + ",");
        println("  \"dataNamed\":" + dataNamed + ",");
        println("  \"alreadyNamedConflicts\":" + conflicts + ",");
        println("  \"failed\":" + failed + ",");
        println("  \"addressesNotInProgram\":" + outside);
        println("}");
    }

    private String q(String s) {
        return "\"" + s.replace("\\", "\\\\").replace("\"", "\\\"") + "\"";
    }
}
