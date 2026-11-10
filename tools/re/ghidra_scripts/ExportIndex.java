// Exports an index of the analysed program: functions, defined strings with
// referencing functions, and RTTI-recovered classes with vtables.
// Usage (headless postScript): ExportIndex.java <outdir>
// @category AoS2
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.address.*;
import ghidra.program.model.data.StringDataInstance;
import java.io.*;
import java.util.*;

public class ExportIndex extends GhidraScript {
	@Override
	protected void run() throws Exception {
		String out = getScriptArgs().length > 0 ? getScriptArgs()[0] : ".";
		new File(out).mkdirs();
		Listing listing = currentProgram.getListing();
		FunctionManager fm = currentProgram.getFunctionManager();
		try (PrintWriter w = new PrintWriter(new FileWriter(out + "/functions.tsv"))) {
			for (Function f : fm.getFunctions(true)) {
				w.printf("%s\t%d\t%s\t%d\n", f.getEntryPoint(), f.getBody().getNumAddresses(),
					f.getName(true), f.getSymbol().getReferenceCount());
			}
		}
		ReferenceManager rm = currentProgram.getReferenceManager();
		try (PrintWriter w = new PrintWriter(new FileWriter(out + "/strings.tsv"))) {
			for (Data d : listing.getDefinedData(true)) {
				if (!d.hasStringValue()) continue;
				String s = String.valueOf(d.getValue()).replace("\n", "\\n").replace("\t", "\\t");
				StringBuilder fns = new StringBuilder();
				for (Reference r : rm.getReferencesTo(d.getAddress())) {
					Function f = fm.getFunctionContaining(r.getFromAddress());
					fns.append(f == null ? r.getFromAddress().toString() : f.getEntryPoint().toString()).append(',');
				}
				w.printf("%s\t%s\t%s\n", d.getAddress(), fns, s);
			}
		}
		SymbolTable st = currentProgram.getSymbolTable();
		try (PrintWriter w = new PrintWriter(new FileWriter(out + "/vtables.tsv"))) {
			SymbolIterator it = st.getSymbolIterator("vftable", true);
			while (it.hasNext()) {
				Symbol s = it.next();
				w.printf("%s\t%s\n", s.getAddress(), s.getParentNamespace().getName(true));
			}
		}
		println("index exported to " + out);
	}
}
