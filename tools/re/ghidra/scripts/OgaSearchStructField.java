// OgaSearchStructField.java — find code that accesses a structure field given a global
// holding the structure.
//
// This is the script that answers Phase 16's question properly:
//   "Show every function that reads offset +0x08 from Force."
//
// ⛔ WHY NOT JUST SEARCH FOR THE OFFSET. Searching for the constant 8 in ARM code returns
// thousands of hits — array indices, loop bounds, unrelated fields, every offset 8 in the
// binary. The Force question is not "where is 8" but "where is a Force loaded, and then
// offset 8 read from it". So this traces the pattern:
//
//     LDR  rX, [global]      <- load the pointer to the object
//     LDR  rY, [rX, #0x08]   <- read the field from it
//
// It reports the containing function, the field offset, and read/write, so a field's
// readers and writers can be enumerated without knowing whether the target is DS ARM,
// PSP MIPS or PS2 Emotion Engine code.
//
// ⛔ IT DOES NOT NAME ANYTHING. A hit is evidence that code touched that offset on that
// object — not proof of what the field means. Naming belongs in per-game findings with
// corroboration, never in a query tool.
//
//@category OGA

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.scalar.Scalar;

import java.util.ArrayList;
import java.util.List;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

public class OgaSearchStructField extends GhidraScript {

    // ARM:  LDR/STR  rY, [rX, #0xNN]   or   [rX], #0xNN  (post-indexed)
    // MIPS: lw/sw    rt, 0xNN(base)
    private static final Pattern ARM_MEM = Pattern.compile(
            "\\b(LDR|LDRB|LDRH|LDRSB|LDRSH|STR|STRB|STRH)\\b\\s+(\\w+)\\s*,\\s*\\[(\\w+)(?:\\s*,\\s*#?(0x[0-9a-fA-F]+|\\d+))?\\]",
            Pattern.CASE_INSENSITIVE);
    private static final Pattern MIPS_MEM = Pattern.compile(
            "\\b(lw|sw|lbu|lhu|lb|lh|sb|sh)\\s+(\\w+)\\s*,\\s*(-?(?:0x[0-9a-fA-F]+|\\d+))\\s*\\(\\s*(\\w+)\\s*\\)",
            Pattern.CASE_INSENSITIVE);

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 2) {
            println("usage: OgaSearchStructField.java <global-symbol-or-address> <field-offset-hex> [max-distance]");
            println("  e.g. OgaSearchStructField.java gForces 0x08");
            println("  e.g. OgaSearchStructField.java 0x021974dc 0x08");
            return;
        }

        String globalArg = args[0];
        long fieldOff = Long.decode(args[1]);
        int maxDistance = args.length > 2 ? Integer.parseInt(args[2]) : 6;

        // Resolve the global to an address.
        List<Address> globals = new ArrayList<>();
        if (globalArg.startsWith("0x") || globalArg.startsWith("0X")) {
            globals.add(toAddr(Long.decode(globalArg)));
        } else {
            var syms = currentProgram.getSymbolTable().getGlobalSymbols(globalArg);
            var it = syms.iterator();
            while (it.hasNext()) globals.add(it.next().getAddress());
            if (globals.isEmpty()) {
                var byName = getSymbols(globalArg, null);
                for (var s : byName) globals.add(s.getAddress());
            }
        }
        if (globals.isEmpty()) {
            println("{\"error\":\"global not found: " + globalArg + "\",\"hits\":[]}");
            return;
        }

        StringBuilder out = new StringBuilder();
        out.append("{\n  \"global\":\"").append(globalArg).append("\",\n");
        out.append("  \"globalAddresses\":[");
        for (int i = 0; i < globals.size(); i++) {
            if (i > 0) out.append(",");
            out.append("\"").append(globals.get(i)).append("\"");
        }
        out.append("],\n  \"fieldOffset\":\"0x").append(Long.toHexString(fieldOff)).append("\",\n");
        out.append("  \"hits\":[\n");

        int hits = 0;
        boolean first = true;

        for (Address gaddr : globals) {
            if (monitor.isCancelled()) break;

            // Walk instructions looking for a load of the global, then a field access
            // from the register it landed in, within a short window.
            Instruction ins = getInstructionAt(gaddr);
            if (ins == null) ins = getFirstInstruction();

            long count = 0;
            long limit = currentProgram.getMemory().getSize();
            while (ins != null && count < limit) {
                if (monitor.isCancelled()) break;
                count++;

                String text = ins.toString();

                // Does this instruction reference the global's address?
                boolean refsGlobal = false;
                for (int i = 0; i < ins.getNumOperands(); i++) {
                    for (Object o : ins.getOpObjects(i)) {
                        if (o instanceof Address) {
                            if (((Address) o).equals(gaddr)) refsGlobal = true;
                        } else if (o instanceof Scalar) {
                            if (((Scalar) o).getUnsignedValue() == gaddr.getOffset()) refsGlobal = true;
                        }
                    }
                }
                if (!refsGlobal && !text.contains(gaddr.toString())) {
                    ins = ins.getNext();
                    continue;
                }

                // Found a reference to the global. Extract the destination register.
                Matcher m = ARM_MEM.matcher(text);
                if (!m.find()) m = MIPS_MEM.matcher(text);
                String baseReg = null, destReg = null;
                if (m.find(0)) {
                    // group(2)=dest, group(3)=base for ARM; group(2)=dest, group(4)=base for MIPS
                    destReg = m.group(2);
                    baseReg = m.groupCount() >= 4 && m.group(3) != null ? m.group(3) : destReg;
                } else {
                    // LDR rX, [pc, #N] style — take the first register named.
                    Matcher regm = Pattern.compile("\\b([a-z]{1,3}\\d{0,2})\\b").matcher(text.toLowerCase());
                    if (regm.find()) destReg = regm.group(1);
                }
                String tracked = destReg != null ? destReg : "r0";

                // Scan the next few instructions for an access to tracked + fieldOff.
                Instruction probe = ins.getNext();
                for (int d = 0; d < maxDistance && probe != null; d++, probe = probe.getNext()) {
                    String pt = probe.toString();
                    Matcher am = ARM_MEM.matcher(pt);
                    Matcher mm = MIPS_MEM.matcher(pt);
                    boolean matched = false;
                    String how = "";
                    boolean isWrite = false;

                    if (am.find()) {
                        String dest = am.group(2), base = am.group(3);
                        String disp = am.group(4);
                        long dispVal = disp == null ? 0 : Long.decode(disp.startsWith("0x") || Character.isDigit(disp.charAt(0)) ? disp : disp);
                        if (base != null && base.equalsIgnoreCase(tracked) && dispVal == fieldOff) {
                            matched = true;
                            how = "ARM " + am.group(1).toUpperCase() + " [base+#disp]";
                            isWrite = am.group(1).toUpperCase().startsWith("STR");
                        }
                    }
                    if (!matched && mm.find()) {
                        String base = mm.group(4);
                        long dispVal = Long.decode(mm.group(3));
                        if (base != null && base.equalsIgnoreCase(tracked) && dispVal == fieldOff) {
                            matched = true;
                            how = "MIPS " + mm.group(1).toLowerCase() + " disp(base)";
                            isWrite = mm.group(1).toLowerCase().startsWith("s");
                        }
                    }
                    if (!matched && pt.toLowerCase().contains(tracked.toLowerCase())
                            && (pt.contains("0x" + Long.toHexString(fieldOff)) || pt.contains("#" + fieldOff))) {
                        matched = true;
                        how = "textual";
                        isWrite = pt.toUpperCase().startsWith("STR") || pt.toLowerCase().startsWith("sw ");
                    }

                    if (matched) {
                        Address at = probe.getAddress();
                        Function f = getFunctionContaining(at);
                        if (!first) out.append(",\n");
                        first = false;
                        out.append("    {\"address\":\"").append(at).append("\"")
                           .append(",\"function\":\"").append(f == null ? "(none)" : f.getName()).append("\"")
                           .append(",\"functionEntry\":\"").append(f == null ? "" : f.getEntryPoint().toString()).append("\"")
                           .append(",\"access\":\"").append(isWrite ? "write" : "read").append("\"")
                           .append(",\"via\":").append(q(how))
                           .append(",\"globalLoad\":").append(q(text))
                           .append(",\"fieldAccess\":").append(q(pt)).append("}");
                        hits++;
                        break;
                    }
                }

                ins = ins.getNext();
            }
        }

        out.append("\n  ],\n  \"count\":").append(hits).append("\n}\n");
        println(out.toString());
    }

    private String q(String s) {
        return "\"" + s.replace("\\", "\\\\").replace("\"", "\\\"") + "\"";
    }
}
