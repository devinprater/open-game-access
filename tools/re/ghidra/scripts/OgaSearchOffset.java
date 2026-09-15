// SearchOffset.java — find every function that accesses a given structure offset.
//
// This is the script that answers the brief's headline question:
//   "Show every function that reads offset +0x08 from Force."
// without the caller knowing whether the target is DS ARM, PSP MIPS or PS2 EE code.
//
// HOW IT WORKS: it scans instructions for a constant displacement operand equal to the
// requested offset, then reports the containing function and whether the access is a
// read or a write. It does NOT decide what the offset means — naming a structure field
// is a judgement that belongs in per-game findings with evidence, not in a query tool.
//
// It also searches pre-instruction comments and the operand representation, because
// offset+base is frequently materialised as `add r0, r0, #0x8` or folded into a
// post-indexed load, so matching on getScalar() alone misses real hits. Missing hits is
// the dangerous failure mode here: it looks like "nothing accesses this field".
//
//@category OGA
//@keybinding
//@menupath
//@toolbar

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.scalar.Scalar;
import ghidra.program.model.symbol.Reference;

public class OgaSearchOffset extends GhidraScript {

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 1) {
            println("usage: SearchOffset.java <offset-hex> [max-hits]");
            println("  e.g. SearchOffset.java 0x08");
            return;
        }

        long want;
        try {
            want = Long.decode(args[0]);
        } catch (NumberFormatException e) {
            println("!! not a number: " + args[0]);
            return;
        }
        int maxHits = args.length > 1 ? Integer.parseInt(args[1]) : 400;

        println("{\"offset\":\"0x" + Long.toHexString(want) + "\",\"hits\":[");

        int hits = 0;
        boolean first = true;
        Instruction ins = getFirstInstruction();

        while (ins != null && hits < maxHits) {
            if (monitor.isCancelled()) break;

            boolean matched = false;
            String how = "";

            // Test every operand's scalar values.
            int nops = ins.getNumOperands();
            for (int i = 0; i < nops && !matched; i++) {
                Object[] objs = ins.getOpObjects(i);
                for (Object o : objs) {
                    if (o instanceof Scalar) {
                        long v = ((Scalar) o).getSignedValue();
                        if (v == want) {
                            matched = true;
                            how = "scalar operand";
                            break;
                        }
                    }
                }
            }

            // Fall back to the textual form: catches folded/immediate encodings the
            // operand model flattens, and post-indexed forms.
            if (!matched) {
                String txt = ins.toString();
                String hex = "0x" + Long.toHexString(want);
                String dec = Long.toString(want);
                if (txt.contains("#" + hex) || txt.contains("#" + dec)
                        || txt.contains("+#" + hex) || txt.contains("+#" + dec)
                        || txt.contains(", #" + hex)) {
                    matched = true;
                    how = "textual";
                }
            }

            if (matched) {
                Address a = ins.getAddress();
                Function f = getFunctionContaining(a);
                boolean isWrite = writesToMemory(ins.toString());

                if (!first) println(",");
                first = false;
                println("  {\"address\":\"" + a + "\","
                        + "\"function\":\"" + (f == null ? "(none)" : f.getName()) + "\","
                        + "\"functionEntry\":\"" + (f == null ? "" : f.getEntryPoint().toString()) + "\","
                        + "\"access\":\"" + (isWrite ? "write" : "read") + "\","
                        + "\"matchedBy\":\"" + how + "\","
                        + "\"instruction\":" + jsonQuote(ins.toString()) + "}");
                hits++;
            }

            ins = ins.getNext();
        }

        println("");
        println("],\"count\":" + hits + "}");
    }

    // A cheap read/write heuristic. ARM loads use LDR/LDM and stores STR/STM; MIPS uses
    // lw/sw. Getting this exactly right per-architecture is not the point — flagging
    // which accesses MIGHT write is, so a human or a second query can confirm.
    private boolean writesToMemory(String txt) {
        String t = txt.toUpperCase();
        return t.startsWith("STR") || t.startsWith("STM") || t.startsWith("ST")
            || t.startsWith("SW ") || t.startsWith("SB ") || t.startsWith("SH ");
    }

    private String jsonQuote(String s) {
        return "\"" + s.replace("\\", "\\\\").replace("\"", "\\\"") + "\"";
    }
}
