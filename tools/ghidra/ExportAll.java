// Headless export of a Ghidra program: function index, disassembly listing
// and decompiled C for every function. Output goes to the directory given as
// the first script argument (defaults to ./re/export).
//
// Usage (headless):
//   analyzeHeadless <projDir> <projName> -process st.exe -noanalysis \
//       -scriptPath tools/ghidra -postScript ExportAll.java re/export
//
//@category SealTeam

import java.io.File;
import java.io.PrintWriter;

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileOptions;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.CodeUnit;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.Reference;

public class ExportAll extends GhidraScript {

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        File outDir = new File(args.length > 0 ? args[0] : "re/export");
        outDir.mkdirs();

        try (PrintWriter mem = new PrintWriter(new File(outDir, "memory_map.txt"))) {
            for (MemoryBlock b : currentProgram.getMemory().getBlocks()) {
                mem.printf("%-16s %s - %s  len=%d%n", b.getName(), b.getStart(), b.getEnd(), b.getSize());
            }
        }

        DecompInterface decomp = new DecompInterface();
        DecompileOptions opts = new DecompileOptions();
        decomp.setOptions(opts);
        decomp.openProgram(currentProgram);

        int count = 0;
        try (PrintWriter idx = new PrintWriter(new File(outDir, "functions.txt"));
             PrintWriter c = new PrintWriter(new File(outDir, "decompiled.c"));
             PrintWriter asm = new PrintWriter(new File(outDir, "disassembly.asm"))) {

            FunctionIterator it = currentProgram.getFunctionManager().getFunctions(true);
            while (it.hasNext() && !monitor.isCancelled()) {
                Function f = it.next();
                long size = f.getBody().getNumAddresses();
                idx.printf("%s\t%s\t%d\t%s%n", f.getEntryPoint(), f.getName(), size, f.getSignature());

                // Disassembly with cross-reference comments
                asm.printf("%n;==== %s @ %s ====%n", f.getName(), f.getEntryPoint());
                InstructionIterator ii = currentProgram.getListing().getInstructions(f.getBody(), true);
                while (ii.hasNext()) {
                    Instruction ins = ii.next();
                    StringBuilder sb = new StringBuilder();
                    sb.append(ins.getAddress()).append("  ").append(ins.toString());
                    for (Reference r : ins.getReferencesFrom()) {
                        if (r.getReferenceType().isCall()) {
                            Function callee = getFunctionAt(r.getToAddress());
                            if (callee != null) sb.append("    ; -> ").append(callee.getName());
                        }
                    }
                    String eol = ins.getComment(CodeUnit.EOL_COMMENT);
                    if (eol != null) sb.append("    ; ").append(eol);
                    asm.println(sb);
                }

                // Decompiled C
                DecompileResults res = decomp.decompileFunction(f, 60, monitor);
                c.printf("%n// ==== %s @ %s (size %d) ====%n", f.getName(), f.getEntryPoint(), size);
                if (res != null && res.decompileCompleted()) {
                    c.println(res.getDecompiledFunction().getC());
                } else {
                    c.printf("// decompile failed: %s%n", res == null ? "null" : res.getErrorMessage());
                }
                count++;
                if (count % 100 == 0) println("exported " + count + " functions");
            }
        }
        decomp.dispose();
        println("ExportAll: exported " + count + " functions to " + outDir.getAbsolutePath());
    }
}
