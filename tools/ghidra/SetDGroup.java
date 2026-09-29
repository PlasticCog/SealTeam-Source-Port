// Pre-analysis script: SEAL Team is a Microsoft C large-model program where
// DS == SS == DGROUP for the whole run (the CRT startup sets it once). Tell
// Ghidra so that near data references resolve into DGROUP.
//
// Usage: -preScript SetDGroup.java [dgroupSegmentHex]   (default 56bf)
//
//@category SealTeam

import java.math.BigInteger;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.lang.Register;
import ghidra.program.model.listing.ProgramContext;
import ghidra.program.model.mem.MemoryBlock;

public class SetDGroup extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        long seg = Long.parseLong(args.length > 0 ? args[0] : "56bf", 16);
        ProgramContext ctx = currentProgram.getProgramContext();
        Register ds = ctx.getRegister("DS");
        Register ss = ctx.getRegister("SS");
        for (MemoryBlock b : currentProgram.getMemory().getBlocks()) {
            if (!b.isInitialized() || b.getName().equals("HEADER")) continue;
            ctx.setValue(ds, b.getStart(), b.getEnd(), BigInteger.valueOf(seg));
            ctx.setValue(ss, b.getStart(), b.getEnd(), BigInteger.valueOf(seg));
        }
        println("SetDGroup: DS=SS=" + Long.toHexString(seg));
    }
}
