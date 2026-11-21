import ghidra.app.decompiler.*;
// Export selected functions, instructions, and references without modifying the program.
// @category GunMaster
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.util.*;

public class ExportEvidence extends GhidraScript {

  @Override
  public void run() throws Exception {
    String[] args = getScriptArgs();
    Set<Function> functions = new LinkedHashSet<>();
    DecompInterface decompiler = new DecompInterface();
    decompiler.openProgram(currentProgram);
    try (PrintWriter out = new PrintWriter(args[0], "UTF-8")) {
      for (int i = 1; i < args.length; i++) {
        boolean defineFunction = args[i].startsWith("function:");
        boolean followReferences = args[i].startsWith("xref:");
        String value = defineFunction
          ? args[i].substring(9)
          : followReferences
            ? args[i].substring(5)
            : args[i];
        Address address = currentProgram
          .getImageBase()
          .add(Long.parseLong(value, 16));
        Function function = getFunctionAt(address);
        if (function == null && !defineFunction) function =
          getFunctionContaining(address);
        if (defineFunction && function == null) {
          disassemble(address);
          function = createFunction(address, null);
        }
        out.println("\nADDRESS " + address + " function " + function);
        ReferenceIterator refs = currentProgram
          .getReferenceManager()
          .getReferencesTo(address);
        int referenceCount = 0;
        while (refs.hasNext()) {
          Reference ref = refs.next();
          if (referenceCount++ < 50) out.println("XREF " + ref);
          Function caller = getFunctionContaining(ref.getFromAddress());
          if (
            caller != null &&
            (followReferences || (function == null && getByte(address) == 'b'))
          ) functions.add(caller);
        }
        out.println("XREF COUNT " + referenceCount);
        if (function != null) functions.add(function);
        else {
          byte[] bytes = new byte[24];
          currentProgram.getMemory().getBytes(address, bytes);
          out.println("DATA " + HexFormat.of().formatHex(bytes));
        }
      }
      for (Function function : functions) {
        out.println(
          "\n=== " +
            function.getEntryPoint() +
            " " +
            function.getName(true) +
            " ==="
        );
        DecompileResults result = decompiler.decompileFunction(
          function,
          45,
          monitor
        );
        if (result.decompileCompleted()) out.println(
          result.getDecompiledFunction().getC()
        );
        else out.println("FAILED " + result.getErrorMessage());
        InstructionIterator instructions = currentProgram
          .getListing()
          .getInstructions(function.getBody(), true);
        while (instructions.hasNext()) {
          Instruction instruction = instructions.next();
          out.println(
            instruction.getAddress() +
              " " +
              HexFormat.of().formatHex(instruction.getBytes()) +
              " " +
              instruction
          );
        }
      }
    } finally {
      decompiler.dispose();
    }
  }
}
