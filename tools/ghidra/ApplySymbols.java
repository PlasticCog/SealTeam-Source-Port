// Apply the reverse-engineered names in tools/re/symbols/*.tsv to the Ghidra
// program: seg_*.tsv rename functions, globals_*.tsv label DGROUP data.
//
// Usage: -postScript ApplySymbols.java <symbolsDir>
//
//@category SealTeam

import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.SourceType;

public class ApplySymbols extends GhidraScript {

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        File dir = new File(args.length > 0 ? args[0] : "tools/re/symbols");
        // Functions come from seg_*.tsv; globals only from the merged table
        // (tools/re/merge_symbols.py), not the per-module globals_*.tsv.
        File[] files = dir.listFiles((d, n) -> n.startsWith("seg_") && n.endsWith(".tsv")
                || n.equals("merged_globals.tsv"));
        if (files == null) {
            printerr("no symbol files in " + dir);
            return;
        }
        int funcs = 0, globals = 0, bad = 0;
        for (File f : files) {
            boolean isGlobal = f.getName().equals("merged_globals.tsv");
            try (BufferedReader r = new BufferedReader(new FileReader(f))) {
                String line;
                while ((line = r.readLine()) != null) {
                    if (line.isBlank() || line.startsWith("#")) continue;
                    String[] cols = line.split("\t");
                    if (cols.length < 2) continue;
                    String name = cols[1].trim();
                    try {
                        if (isGlobal) {
                            Address a = toAddr("56bf:" + cols[0].trim());
                            createLabel(a, name, true, SourceType.USER_DEFINED);
                            String desc = cols.length > 3 ? cols[3].trim() : "";
                            if (!desc.isEmpty()) setEOLComment(a, desc);
                            globals++;
                        } else {
                            Address a = toAddr(cols[0].trim());
                            Function fn = getFunctionAt(a);
                            if (fn == null) fn = createFunction(a, name);
                            if (fn == null) { bad++; continue; }
                            fn.setName(name, SourceType.USER_DEFINED);
                            if (cols.length > 2 && !cols[2].isBlank()) fn.setComment(cols[2].trim());
                            funcs++;
                        }
                    } catch (Exception e) {
                        bad++;
                        println("skip " + f.getName() + ": " + line + " (" + e.getMessage() + ")");
                    }
                }
            }
        }
        println("ApplySymbols: " + funcs + " functions, " + globals + " globals, " + bad + " skipped");
    }
}
