import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.SourceType;

import java.nio.file.Files;
import java.nio.file.Paths;
import java.util.List;

public class ApplySymbols extends GhidraScript {
    @Override
    protected void run() throws Exception {
        int applied = 0;
        int skipped = 0;
        for (String path : getScriptArgs()) {
            if (!Files.isRegularFile(Paths.get(path))) {
                println("symbol file not found; skipping: " + path);
                continue;
            }
            List<String> lines = Files.readAllLines(Paths.get(path));
            for (int index = 1; index < lines.size(); index++) {
                String line = lines.get(index).trim();
                if (line.isEmpty()) {
                    continue;
                }
                String[] fields = line.split(",", 2);
                if (fields.length < 2) {
                    skipped++;
                    continue;
                }
                String rawAddress = fields[0].trim();
                String name = fields[1].trim();
                try {
                    if (!rawAddress.matches("(?i)0x[0-9a-f]+") || name.isEmpty()) {
                        skipped++;
                        continue;
                    }
                    long value = Long.parseLong(rawAddress.substring(2), 16);
                    if (value < 0 || value > 0xFFFFFFFFL) {
                        skipped++;
                        continue;
                    }
                    if (apply(toAddr(value), name)) {
                        applied++;
                    } else {
                        skipped++;
                    }
                } catch (Exception error) {
                    skipped++;
                    if (skipped <= 10) {
                        println("skipping malformed symbol row " + (index + 1) + ": " + error.getMessage());
                    }
                }
            }
        }
        println("applied " + applied + " symbols; skipped " + skipped + " invalid or conflicting rows");
    }

    private boolean apply(Address address, String name) {
        Function function = getFunctionAt(address);
        try {
            if (function != null) {
                function.setName(name, SourceType.USER_DEFINED);
            } else {
                createLabel(address, name, true, SourceType.USER_DEFINED);
            }
            return true;
        } catch (Exception duplicate) {
            String unique = name + "_" + address;
            try {
                if (function != null) {
                    function.setName(unique, SourceType.USER_DEFINED);
                } else {
                    createLabel(address, unique, true, SourceType.USER_DEFINED);
                }
                return true;
            } catch (Exception failure) {
                return false;
            }
        }
    }
}
