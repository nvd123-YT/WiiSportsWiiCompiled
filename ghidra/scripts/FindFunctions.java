import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;

import java.util.ArrayList;
import java.util.List;
import java.util.TreeSet;

public class FindFunctions extends GhidraScript {
    private static final int MFLR_R0 = 0x7C0802A6;
    private static final int STWU_R1_MASK = 0xFFFF0000;
    private static final int STWU_R1 = 0x94210000;
    private static final int STW_R0_4_R1 = 0x90010004;

    private Memory memory;

    @Override
    protected void run() throws Exception {
        memory = currentProgram.getMemory();
        int created;
        int total = 0;
        int failures = 0;
        List<String> failureSamples = new ArrayList<>();
        do {
            created = 0;
            for (long start : collectCandidates()) {
                monitor.checkCancelled();
                Address address = toAddr(start);
                if (currentProgram.getFunctionManager().getFunctionContaining(address) != null) {
                    continue;
                }
                try {
                    if (!memory.getBlock(address).isExecute() || memory.getInt(address) == 0) {
                        continue;
                    }
                    if (!disassemble(address)) {
                        continue;
                    }
                    Function function = createFunction(address, null);
                    if (function != null) {
                        created++;
                    }
                } catch (Exception error) {
                    failures++;
                    if (failureSamples.size() < 20) {
                        failureSamples.add("0x" + Long.toHexString(start) + ": "
                                + error.getClass().getSimpleName() + ": " + error.getMessage());
                    }
                }
            }
            total += created;
            println("created " + created + " functions in this pass");
        } while (created > 0);
        println("total discovered functions: " + total);
        if (failures > 0) {
            println("skipped " + failures + " function candidates that Ghidra could not decode:");
            for (String sample : failureSamples) {
                println("  " + sample);
            }
            if (failures > failureSamples.size()) {
                println("  ... " + (failures - failureSamples.size()) + " additional candidate errors omitted");
            }
        }
    }

    private TreeSet<Long> collectCandidates() throws Exception {
        TreeSet<Long> candidates = new TreeSet<>();
        for (MemoryBlock block : memory.getBlocks()) {
            if (!block.isExecute() || !block.isInitialized()) {
                continue;
            }
            long begin = block.getStart().getOffset();
            long end = block.getEnd().getOffset();
            for (long address = begin; address + 3 <= end; address += 4) {
                monitor.checkCancelled();
                int word = memory.getInt(toAddr(address));
                if ((word >>> 26) == 18 && (word & 1) == 1) {
                    long displacement = ((long) (word << 6) >> 6) & ~3L;
                    long target = (word & 2) != 0
                            ? displacement & 0xFFFFFFFFL
                            : address + displacement;
                    if ((target & 3) == 0 && target >= begin && target + 3 <= end) {
                        candidates.add(target);
                    }
                } else if (word == MFLR_R0) {
                    long start = prologueStart(address, begin, end);
                    if (start >= 0) {
                        candidates.add(start);
                    }
                }
            }
        }
        return candidates;
    }

    private long prologueStart(long mflr, long begin, long end) throws Exception {
        if (mflr - 4 >= begin && (memory.getInt(toAddr(mflr - 4)) & STWU_R1_MASK) == STWU_R1) {
            return mflr - 4;
        }
        for (long next = mflr + 4; next <= mflr + 12 && next + 3 <= end; next += 4) {
            int word = memory.getInt(toAddr(next));
            if ((word & STWU_R1_MASK) == STWU_R1 || word == STW_R0_4_R1) {
                return mflr;
            }
        }
        return -1;
    }
}
