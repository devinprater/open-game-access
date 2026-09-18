// DisScanStringRefs.java -- find MIPS code that loads the address of Dissidia's resource-name strings.
//
// !!! SUPERSEDED -- ITS TARGETS USE THE WRONG ADDRESS SPACE, SO IT ALWAYS REPORTS 0 MATCHES !!!
//
// WHY IT FAILS (third instance of this class of bug in this project)
//   The TARGETS below are RAM-space addresses (0x08B75F68 ...), but the DISSIDIA_ELF project was
//   imported with ElfLoader, so its listing uses ELF VADDR space (0x00371F68 ...). The two differ by
//   the load base 0x08804000 -- confirmed independently in doc section 19:
//
//       RAM-space target  0x08B75F68
//       equivalent vaddr  0x00371F68      <- what the listing actually contains
//
//   Searching for 0x08B7xxxx therefore matches NOTHING, in EITHER project, which reads as "the code
//   never references these strings". The same scan reported 0 across 783,694 instructions.
//
// WHAT TO USE INSTEAD
//   DisFindStrings.java -- it does not precompute addresses at all; it SEARCHES Ghidra's memory for
//   the string BYTES, so the address it uses is by construction the one in the listing. That run
//   found 23 string occurrences and decompiled 10 referring functions (doc section 15).
//
//   Rule (skill Rule 90): never precompute a string's address to find its referrers -- search the
//   listing for the bytes. Precomputing is how the address space gets mismatched.
//
// Kept as a record of the bug. Do not run it expecting results.
//
// USAGE (historical)
//   analyzeHeadless <proj> DISSIDIA -process EBOOT.dec -noanalysis
//       -scriptPath "<proj>" -postScript DisScanStringRefs.java
//
// @category OGA

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.scalar.Scalar;

import java.io.PrintWriter;
import java.io.File;
import java.util.*;

public class DisScanStringRefs extends GhidraScript {

    /** (address, label) pairs. The project is now imported with ElfLoader at base 0x08804000, so
     *  Ghidra addresses are base + ELF vaddr. For the first LOAD segment (file_off 0x74,
     *  vaddr 0x00000000) that is base + file_offset - 0x74. The old BinaryLoader import used
     *  base + file_offset, which skewed every address by 0x74 and made this scan report 0 matches
     *  across 683,649 instructions. */
    private static final Object[][] TARGETS = {
        { 0x08B75F68L, "pause_help.bin" },
        { 0x08B828CCL, "pause_help.bin#2" },
        { 0x08B8B99CL, "pause_help.bin#3" },
        { 0x08B7B82CL, "item_help" },
        { 0x08B7B628L, "accessory_help" },
        { 0x08B760ADL, "name.bin" },
        { 0x08B7B8A0L, "system.bin" },
        { 0x08B75FDCL, "battle_result.bin" },
        { 0x08B75FB0L, "SYSTEM_FONT::Draw" },
        { 0x08B75F90L, "libfont.prx" },
        { 0x08B761A8L, "VOLATILE_MEMORY_LOADER" },
    };

    @Override
    public void run() throws Exception {
        String outPath = System.getProperty("user.home")
                + "/oga-ghidra-dissidia/stringrefs-report.txt";
        PrintWriter out = new PrintWriter(new File(outPath), "UTF-8");

        // Build a lookup: HI16 -> list of (target, LO16)
        Map<Integer, List<Object[]>> byHi = new HashMap<>();
        for (Object[] t : TARGETS) {
            long a = (Long) t[0];
            String label = (String) t[1];
            int hi = (int) (((a + 0x8000) >> 16) & 0xFFFF);   // MIPS signed-addend carry adjustment
            int lo = (int) (a & 0xFFFF);
            byHi.computeIfAbsent(hi, k -> new ArrayList<>()).add(new Object[] { lo, label, a });
            // also the naive pairing, in case the compiler did not carry
            int hi2 = (int) ((a >> 16) & 0xFFFF);
            if (hi2 != hi) {
                byHi.computeIfAbsent(hi2, k -> new ArrayList<>()).add(new Object[] { lo, label + "~naive", a });
            }
        }

        out.println("target HI16 values: " + byHi.keySet());
        out.println();

        int matches = 0;
        int scanned = 0;

        for (MemoryBlock b : currentProgram.getMemory().getBlocks()) {
            if (!b.isInitialized() || !b.isExecute()) continue;
            InstructionIterator it = currentProgram.getListing().getInstructions(b.getStart(), true);
            Instruction prev = null;
            while (it.hasNext()) {
                Instruction ins = it.next();
                if (ins.getAddress().compareTo(b.getEnd()) > 0) break;
                scanned++;


                // Collect every scalar operand safely.
                List<Long> scalars = new ArrayList<>();
                for (int k = 0; k < ins.getNumOperands(); k++) {
                    Object[] objs = ins.getOpObjects(k);
                    if (objs == null) continue;
                    for (Object o : objs) {
                        if (o instanceof Scalar) {
                            scalars.add(((Scalar) o).getUnsignedValue());
                        }
                    }
                }

                // LUI rt, hi -- identify by mnemonic, NOT by opcode: Ghidra's Instruction
                // interface has no getOpCode(), and the MIPS opcode byte is 0x0F for LUI.
                String mnem = ins.getMnemonicString();
                if ("lui".equalsIgnoreCase(mnem) && !scalars.isEmpty()) {
                    long hi = scalars.get(0) & 0xFFFF;
                    List<Object[]> cands = byHi.get((int) hi);
                    if (cands != null) {
                        for (Object[] c : cands) {
                            out.println("LUI @ " + ins.getAddress()
                                    + "  hi=0x" + Long.toHexString(hi)
                                    + "  -> candidate \"" + c[1] + "\"");
                            matches++;
                        }
                    }
                }

                // Any scalar equal to a full target address.
                for (long v : scalars) {
                    for (Object[] t : TARGETS) {
                        if (v == (Long) t[0]) {
                            out.println("EXACT @ " + ins.getAddress() + "  " + ins
                                    + "  -> \"" + t[1] + "\"");
                            matches++;
                        }
                    }
                }
                prev = ins;
            }
        }

        out.println();
        out.println("### scanned " + scanned + " instructions, " + matches + " candidate match(es)");
        out.flush();
        out.close();
        println("DisScanStringRefs -> " + outPath + "  (" + matches + " matches)");
    }
}
