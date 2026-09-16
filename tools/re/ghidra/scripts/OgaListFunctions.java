// OgaListFunctions.java — enumerate functions with addresses, sizes, and names.
//
// Reports JSON so the result can be filtered and counted programmatically rather than
// read by eye. Note the explicit autoNamed flag: a name like FUN_02042a10 is Ghidra's
// placeholder, NOT knowledge. Downstream consumers must be able to tell an auto-named
// function from a human-named one, because treating the former as understood is how a
// decompiler's guess becomes a false fact.
//
//@category OGA

import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;

public class OgaListFunctions extends GhidraScript {

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        // Optional filter: a name substring, case-insensitive.
        String filter = args.length > 0 ? args[0].toLowerCase() : null;

        FunctionIterator it = currentProgram.getFunctionManager().getFunctions(true);

        int total = 0, autoNamed = 0, shown = 0;
        StringBuilder sb = new StringBuilder();
        boolean first = true;

        while (it.hasNext()) {
            if (monitor.isCancelled()) break;
            Function f = it.next();
            total++;

            String name = f.getName();
            boolean auto = name.startsWith("FUN_") || name.startsWith("thunk_FUN_")
                        || name.startsWith("LAB_") || name.startsWith("SUB_");
            if (auto) autoNamed++;

            if (filter != null && !name.toLowerCase().contains(filter)) continue;

            if (!first) sb.append(",\n");
            first = false;
            sb.append("  {\"name\":").append(q(name))
              .append(",\"entry\":\"").append(f.getEntryPoint()).append("\"")
              .append(",\"size\":").append(f.getBody().getNumAddresses())
              .append(",\"autoNamed\":").append(auto)
              .append(",\"thunk\":").append(f.isThunk())
              .append(",\"external\":").append(f.isExternal())
              .append("}");
            shown++;
        }

        println("{");
        println("  \"program\":\"" + currentProgram.getName() + "\",");
        println("  \"language\":\"" + currentProgram.getLanguageID() + "\",");
        println("  \"totalFunctions\":" + total + ",");
        println("  \"autoNamedFunctions\":" + autoNamed + ",");
        println("  \"namedFunctions\":" + (total - autoNamed) + ",");
        println("  \"shown\":" + shown + ",");
        println("  \"functions\":[");
        println(sb.toString());
        println("  ]");
        println("}");
    }

    private String q(String s) {
        return "\"" + s.replace("\\", "\\\\").replace("\"", "\\\"") + "\"";
    }
}
