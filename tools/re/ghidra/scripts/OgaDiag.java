// OgaDiag.java — diagnostic: what is actually in this program?
//
// Written after a "Force.id reader" search returned 0 hits three times. Zero hits has
// several possible causes and they need distinguishing rather than guessing:
//   1. the address is not mapped (no memory block)
//   2. the symbol does not exist at that address
//   3. the code genuinely never references it
//   4. the reference exists but not in the instruction pattern the search expects
// This prints enough to tell which.
//
//@category OGA

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolIterator;

public class OgaDiag extends GhidraScript {

    @Override
    public void run() throws Exception {
        println("=== memory blocks ===");
        for (MemoryBlock b : currentProgram.getMemory().getBlocks()) {
            println(String.format("  %-12s %s - %s  size=%d  init=%s  r=%s w=%s x=%s",
                    b.getName(), b.getStart(), b.getEnd(), b.getSize(),
                    b.isInitialized(), b.isRead(), b.isWrite(), b.isExecute()));
        }

        String[] args = getScriptArgs();
        long probe = args.length > 0 ? Long.decode(args[0]) : 0x021974dcL;
        Address a = toAddr(probe);
        println("\n=== probe " + a + " ===");
        println("  memory contains: " + currentProgram.getMemory().contains(a));
        println("  block: " + currentProgram.getMemory().getBlock(a));

        Symbol[] syms = currentProgram.getSymbolTable().getSymbols(a);
        println("  symbols at that address: " + syms.length);
        for (Symbol s : syms) println("    " + s.getName() + "  (" + s.getSymbolType() + ")");

        // How many symbols reference this address from anywhere?
        SymbolIterator it = currentProgram.getSymbolTable().getSymbolIterator();
        int refs = 0;
        while (it.hasNext()) {
            Symbol s = it.next();
            if (s.getAddress().equals(a)) refs++;
        }
        println("  symbolIterator matches at address: " + refs);

        // Count instructions that mention the address textually.
        println("\n=== instruction scan (textual) ===");
        int total = 0, mentionHex = 0, mentionName = 0;
        String hex = "0x" + Long.toHexString(probe);
        String shortHex = Long.toHexString(probe).replaceFirst("^0+", "");
        Instruction ins = getFirstInstruction();
        while (ins != null && total < 400000) {
            if (monitor.isCancelled()) break;
            total++;
            String t = ins.toString().toLowerCase();
            if (t.contains(hex) || t.contains(shortHex)) {
                mentionHex++;
                if (mentionHex <= 6) {
                    Function f = getFunctionContaining(ins.getAddress());
                    println("  hit @" + ins.getAddress() + "  [" + (f == null ? "-" : f.getName()) + "]  " + ins);
                }
            }
            ins = ins.getNext();
        }
        println("  instructions scanned: " + total);
        println("  instructions mentioning the address textually: " + mentionHex);

        println("\n=== symbols whose name contains 'orce' ===");
        SymbolIterator si = currentProgram.getSymbolTable().getSymbolIterator();
        int n = 0;
        while (si.hasNext()) {
            Symbol s = si.next();
            if (s.getName().toLowerCase().contains("orce")) {
                println("  " + s.getName() + " @ " + s.getAddress());
                if (++n > 12) break;
            }
        }
        if (n == 0) println("  (none)");
    }
}
