import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;

import java.io.ByteArrayInputStream;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.util.Arrays;
import java.util.Comparator;

public class LoadDol extends GhidraScript {
    private static final int HEADER_SIZE = 0x100;
    private static final int TEXT_SECTIONS = 7;
    private static final int TOTAL_SECTIONS = 18;
    private static final long ADDRESS_SPACE_END = 0x100000000L;

    @Override
    protected void run() throws Exception {
        if (getScriptArgs().length < 1) {
            throw new IOException("LoadDol requires the path to the Wii Sports main.dol");
        }
        byte[] file = Files.readAllBytes(Paths.get(getScriptArgs()[0]));
        if (file.length < HEADER_SIZE) {
            throw new IOException("Invalid DOL: file is shorter than the 0x100-byte header");
        }

        ByteBuffer header = ByteBuffer.wrap(file);
        Memory memory = currentProgram.getMemory();

        for (MemoryBlock block : memory.getBlocks()) {
            memory.removeBlock(block, monitor);
        }

        for (int i = 0; i < TOTAL_SECTIONS; i++) {
            long fileOffset = unsigned(header.getInt(i * 4));
            long address = unsigned(header.getInt(0x48 + i * 4));
            long sectionSize = unsigned(header.getInt(0x90 + i * 4));
            if (sectionSize == 0) {
                continue;
            }
            if (fileOffset < HEADER_SIZE || fileOffset + sectionSize > file.length
                    || sectionSize > Integer.MAX_VALUE) {
                throw new IOException("Invalid DOL section " + i + ": file range 0x"
                        + Long.toHexString(fileOffset) + "+0x" + Long.toHexString(sectionSize)
                        + " exceeds file size 0x" + Integer.toHexString(file.length));
            }
            if (address + sectionSize > ADDRESS_SPACE_END) {
                throw new IOException("Invalid DOL section " + i + ": guest address range wraps");
            }

            boolean executable = i < TEXT_SECTIONS;
            String name = executable ? ".text" + i : ".data" + (i - TEXT_SECTIONS);
            byte[] bytes = Arrays.copyOfRange(file, (int) fileOffset, (int) (fileOffset + sectionSize));
            MemoryBlock block = memory.createInitializedBlock(
                    name, toAddr(address), new ByteArrayInputStream(bytes), sectionSize, monitor, false);
            block.setRead(true);
            block.setWrite(!executable);
            block.setExecute(executable);
        }

        long bssAddress = unsigned(header.getInt(0xD8));
        long bssSize = unsigned(header.getInt(0xDC));
        if (bssAddress + bssSize > ADDRESS_SPACE_END) {
            throw new IOException("Invalid DOL BSS range: guest address range wraps");
        }
        createUnmappedBss(memory, bssAddress, bssSize);

        long entryValue = unsigned(header.getInt(0xE0));
        if ((entryValue & 3) != 0) {
            throw new IOException("Invalid DOL entry point: address is not 4-byte aligned");
        }
        Address entry = toAddr(entryValue);
        MemoryBlock entryBlock = memory.getBlock(entry);
        if (entryBlock == null || !entryBlock.isExecute()) {
            throw new IOException("Invalid DOL entry point 0x" + Long.toHexString(entryValue)
                    + ": it is not inside an executable section");
        }
        Function entryFunction = createFunction(entry, "entry");
        if (entryFunction == null) {
            throw new IOException("Ghidra could not create the DOL entry-point function at " + entry);
        }
        currentProgram.getSymbolTable().addExternalEntryPoint(entry);
        println("Loaded DOL sections into guest memory; entry point " + entry);
    }

    private long unsigned(int value) {
        return Integer.toUnsignedLong(value);
    }

    private void createUnmappedBss(Memory memory, long address, long size) throws Exception {
        if (size == 0) {
            return;
        }
        long cursor = address;
        long end = address + size;
        MemoryBlock[] blocks = memory.getBlocks();
        Arrays.sort(blocks, Comparator.comparingLong(block -> block.getStart().getOffset()));
        int part = 0;
        for (MemoryBlock block : blocks) {
            long blockStart = block.getStart().getOffset();
            long blockEnd = block.getEnd().getOffset() + 1;
            if (blockEnd <= cursor || blockStart >= end) {
                continue;
            }
            if (blockStart > cursor) {
                createBssBlock(memory, part++, cursor, Math.min(blockStart, end) - cursor);
            }
            cursor = Math.max(cursor, blockEnd);
            if (cursor >= end) {
                return;
            }
        }
        if (cursor < end) {
            createBssBlock(memory, part, cursor, end - cursor);
        }
    }

    private void createBssBlock(Memory memory, int part, long address, long size) throws Exception {
        if (size <= 0) {
            return;
        }
        MemoryBlock bss = memory.createUninitializedBlock(".bss" + part, toAddr(address), size, false);
        bss.setRead(true);
        bss.setWrite(true);
        bss.setExecute(false);
    }
}
