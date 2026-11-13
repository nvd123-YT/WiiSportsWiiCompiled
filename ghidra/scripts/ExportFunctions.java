import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;

public class ExportFunctions extends GhidraScript {
    @Override
    protected void run() throws Exception {
        if (getScriptArgs().length < 1) {
            throw new IOException("ExportFunctions requires an output CSV path");
        }
        Path output = Paths.get(getScriptArgs()[0]);
        Path parent = output.toAbsolutePath().getParent();
        if (parent != null) {
            Files.createDirectories(parent);
        }
        int count = 0;
        try (java.io.BufferedWriter out = Files.newBufferedWriter(output, StandardCharsets.UTF_8)) {
            out.write("address,size\n");
            FunctionIterator functions = currentProgram.getFunctionManager().getFunctions(true);
            while (functions.hasNext()) {
                Function function = functions.next();
                out.write(String.format("0x%08x,%d%n",
                        function.getEntryPoint().getOffset(), function.getBody().getNumAddresses()));
                count++;
            }
        }
        if (count == 0) {
            throw new IOException("Ghidra discovered no functions; refusing to write an empty function export");
        }
        println("exported " + count + " functions to " + output);
    }
}
