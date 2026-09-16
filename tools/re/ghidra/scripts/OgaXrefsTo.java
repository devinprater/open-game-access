// OgaXrefsTo.java — find everything that references a symbol or address.
//
// ⛔ WHY TEXTUAL SEARCHING FAILS ON ARM AND THIS IS THE RIGHT TOOL.
// A search for the literal text of a bss address finds nothing, because ARM code does not
// encode the address in the instruction. It loads it from a LITERAL POOL:
//
//     LDR  r0, [pc, #0x14]     <- loads the VALUE stored nearby
//     LDR  r1, [r0, #0x8]      <- reads the field
//     ...
//     .word 0x021974dc         <- the literal pool holds the address
//
// So "which code touches this global" is answered by the reference database, not by
// scanning instruction text. Ghidra resolves the pool literal into a real reference once
// the address is mapped and named — which is why mapping .bss first was mandatory.
//
// This reports each reference with its type (read/write/data/flow), the containing
// function, and the instruction, so a field's readers and writers can be enumerated.
//
//@category OGA

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.symbol.Symbol;

public class OgaXrefsTo extends GhidraScript {

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 1) {
            println("usage: OgaXrefsTo.java <symbol-name|0xADDRESS> [max]");
            println("  e.g. OgaXrefsTo.java gForces");
            return;
        }
        String arg = args[0];
        int max = args.length > 1 ? Integer.parseInt(args[1]) : 300;

        Address target = null;
        if (arg.startsWith("0x") || arg.startsWith("0X")) {
            target = toAddr(Long.decode(arg));
        } else {
            // ⛔ getGlobalSymbols returns a List in this Ghidra version, not an array.
            // Declaring Symbol[] fails to compile, and the headless error for a compile
            // failure is "The class could not be found" — which looks like a missing
            // script rather than a type error. Check the log for the real javac message.
            java.util.List<Symbol> syms = currentProgram.getSymbolTable().getGlobalSymbols(arg);
            if (!syms.isEmpty()) {
                target = syms.get(0).getAddress();
            } else {
                var it = getSymbols(arg, null);
                if (!it.isEmpty()) target = it.get(0).getAddress();
            }
        }
        if (target == null) {
            println("{\"error\":\"not found: " + arg + "\",\"refs\":[]}");
            return;
        }

        StringBuilder out = new StringBuilder();
        out.append("{\n  \"target\":\"").append(arg).append("\",\n");
        out.append("  \"address\":\"").append(target).append("\",\n");
        out.append("  \"mapped\":").append(currentProgram.getMemory().contains(target)).append(",\n");
        out.append("  \"refs\":[\n");

        int n = 0;
        boolean first = true;
        ReferenceIterator refs = currentProgram.getReferenceManager().getReferencesTo(target);
        while (refs.hasNext() && n < max) {
            if (monitor.isCancelled()) break;
            Reference r = refs.next();
            Address from = r.getFromAddress();
            Function f = getFunctionContaining(from);
            Instruction ins = getInstructionAt(from);
            if (!first) out.append(",\n");
            first = false;
            out.append("    {\"from\":\"").append(from).append("\"")
               .append(",\"type\":\"").append(r.getReferenceType().toString()).append("\"")
               .append(",\"isWrite\":").append(r.getReferenceType().isWrite())
               .append(",\"function\":\"").append(f == null ? "(none)" : f.getName()).append("\"")
               .append(",\"functionEntry\":\"").append(f == null ? "" : f.getEntryPoint().toString()).append("\"")
               .append(",\"instruction\":").append(q(ins == null ? "(data)" : ins.toString()))
               .append("}");
            n++;
        }
        out.append("\n  ],\n  \"count\":").append(n).append("\n}\n");
        println(out.toString());
    }

    private String q(String s) {
        return "\"" + s.replace("\\", "\\\\").replace("\"", "\\\"") + "\"";
    }
}
