import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import java.util.HashSet;
import java.util.Set;

public class ExportWeatherFunctions extends GhidraScript {
    @Override
    protected void run() throws Exception {
        long[] addresses = {
            0x0040b9d0L,
            0x0040bc70L,
            0x0040ce60L,
            0x0040cee0L,
            0x0040d340L,
            0x0040d850L,
            0x0040dad0L,
            0x004105e0L,
            0x00410950L,
            0x00412e60L,
            0x00412fb0L,
            0x004128a0L
        };
        DecompInterface decompiler = new DecompInterface();
        decompiler.openProgram(currentProgram);
        try {
            Set<String> visited = new HashSet<>();
            for (long address : addresses) {
                Function function = currentProgram.getFunctionManager()
                    .getFunctionContaining(toAddr(address));
                println("\n===== ADDRESS 0x" + Long.toHexString(address) +
                    " FUNCTION " + (function == null ? "<none>" : function.getName()) +
                    " ENTRY " + (function == null ? "<none>" :
                        function.getEntryPoint()) + " =====");
                if (function == null ||
                    !function.getEntryPoint().equals(toAddr(address)) ||
                    !visited.add(function.getEntryPoint().toString())) {
                    println("SKIPPED: not a distinct function entry");
                    continue;
                }
                DecompileResults results =
                    decompiler.decompileFunction(function, 120, monitor);
                if (results.decompileCompleted() &&
                    results.getDecompiledFunction() != null) {
                    println(results.getDecompiledFunction().getC());
                } else {
                    println("DECOMPILATION FAILED: " + results.getErrorMessage());
                }
            }
        } finally {
            decompiler.dispose();
        }
    }
}
