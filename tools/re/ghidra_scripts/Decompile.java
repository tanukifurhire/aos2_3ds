// Decompiles the functions listed (one hex address per line) in <listfile>
// into <outfile>. Usage: Decompile.java <listfile> <outfile>
// @category AoS2
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.address.*;
import java.io.*;
import java.nio.file.*;

public class Decompile extends GhidraScript {
	@Override
	protected void run() throws Exception {
		String[] a = getScriptArgs();
		DecompInterface d = new DecompInterface();
		d.openProgram(currentProgram);
		FunctionManager fm = currentProgram.getFunctionManager();
		try (PrintWriter w = new PrintWriter(new FileWriter(a[1]))) {
			for (String line : Files.readAllLines(Paths.get(a[0]))) {
				line = line.trim();
				if (line.isEmpty() || line.startsWith("#")) continue;
				Address addr = toAddr(Long.parseLong(line.replace("0x", ""), 16));
				Function f = fm.getFunctionContaining(addr);
				if (f == null) { w.println("// no function at " + line); continue; }
				DecompileResults r = d.decompileFunction(f, 120, monitor);
				w.println("// ===== " + f.getName(true) + " @ " + f.getEntryPoint());
				w.println(r.decompileCompleted() ? r.getDecompiledFunction().getC() : "// FAILED: " + r.getErrorMessage());
			}
		}
		d.dispose();
	}
}
