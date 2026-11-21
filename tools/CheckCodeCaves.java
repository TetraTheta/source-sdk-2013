// @category GunMaster
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;

/** Reject padding ranges containing known functions or incoming references. */
public class CheckCodeCaves extends GhidraScript {

  @Override
  public void run() throws Exception {
    for (String value : getScriptArgs()) {
      String[] parts = value.split(":");
      Address start = currentProgram
        .getImageBase()
        .add(Long.parseLong(parts[0], 16));
      int length = Integer.parseInt(parts[1]);
      for (int i = 0; i < length; i++) {
        Address address = start.add(i);
        if (
          getByte(address) != (byte) 0xcc ||
          getFunctionContaining(address) != null ||
          currentProgram
            .getReferenceManager()
            .getReferencesTo(address)
            .hasNext()
        ) {
          throw new IllegalStateException(
            "Not unreferenced padding: " + address
          );
        }
      }
      println("PASS unreferenced INT3 padding " + start + " length " + length);
    }
  }
}
