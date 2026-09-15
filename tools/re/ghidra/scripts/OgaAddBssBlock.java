// OgaAddBssBlock.java — create a memory block for a module's .bss range.
//
// ⛔ WHY THIS IS NECESSARY FOR ANY STRUCTURE QUERY.
// `dsd rom extract` gives us arm9.bin, which contains only the FILE bytes. The module's
// .bss (0x020E3CA0 -> 0x021A23E0, ~768 KB) has no bytes in the file by definition, so
// Ghidra has no memory there at all. Every global that lives in bss — gUnitList,
// gForces, gFE11Database — then resolves to an address Ghidra considers nonexistent, and
// a "which code reads Force.id" search silently returns ZERO HITS.
//
// That failure mode is the dangerous kind: zero hits reads as "nothing does this", when
// the truth is "this address is not mapped". Creating an uninitialised block gives the
// address a place to live so references resolve and cross-references work.
//
// The range comes from the decomp's delinks.txt (the authoritative section layout), and
// the block is created with no initialised bytes, which is what bss actually is.
//
//@category OGA

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.mem.MemoryConflictException;

public class OgaAddBssBlock extends GhidraScript {

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 2) {
            println("usage: OgaAddBssBlock.java <start-addr> <end-addr> [name]");
            println("  e.g. OgaAddBssBlock.java 0x020e3ca0 0x021a23e0 .bss");
            return;
        }
        long start = Long.decode(args[0]);
        long end = Long.decode(args[1]);
        String name = args.length > 2 ? args[2] : ".bss";

        if (end <= start) {
            println("{\"error\":\"end must be greater than start\"}");
            return;
        }

        Memory mem = currentProgram.getMemory();
        Address s = toAddr(start);
        Address e = toAddr(end - 1);

        // Already mapped? Report and stop rather than duplicating.
        MemoryBlock existing = mem.getBlock(s);
        if (existing != null && mem.getBlock(e) == existing) {
            println("{\"status\":\"already-mapped\",\"block\":\"" + existing.getName()
                    + "\",\"start\":\"" + existing.getStart() + "\",\"end\":\"" + existing.getEnd() + "\"}");
            return;
        }

        try {
            MemoryBlock b = mem.createUninitializedBlock(name, s, end - start, false);
            b.setRead(true);
            b.setWrite(true);
            b.setExecute(false);
            println("{");
            println("  \"status\":\"created\",");
            println("  \"name\":\"" + b.getName() + "\",");
            println("  \"start\":\"" + b.getStart() + "\",");
            println("  \"end\":\"" + b.getEnd() + "\",");
            println("  \"size\":" + b.getSize());
            println("}");
        } catch (MemoryConflictException ex) {
            println("{\"status\":\"conflict\",\"detail\":\"" + ex.getMessage() + "\"}");
        }
    }
}
