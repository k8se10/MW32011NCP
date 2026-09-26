// UnwindInfoLookup.java -- resolves a queried address against the REAL PE .pdata
// (x64 SEH exception-unwind) table directly, to answer the question this project's
// own 2026-09-26 sun-shadow/render-thread-fallback investigation hit the hard way:
// "is this address a genuine, independently-hookable function entry, or a shared
// epilogue/tail-call fragment that only LOOKS like a function to Ghidra's own
// (.pdata-derived) function-boundary detection?"
//
// Real background: x64 Windows requires every function that can appear in a stack
// unwind to have a RUNTIME_FUNCTION entry in .pdata (12 bytes: BeginAddress RVA,
// EndAddress RVA, UnwindInfoAddress RVA). MSVC's optimizer routinely factors an
// IDENTICAL epilogue sequence shared by several real functions out into its own
// small code block, reached via JMP (a genuine tail call) rather than CALL -- that
// shared block gets its OWN real RUNTIME_FUNCTION entry (so the unwinder can still
// walk through it correctly), but it is NOT a real, independently-callable function:
// it has no prologue of its own (SizeOfProlog == 0, or the whole thing IS the
// epilogue), and MinHook's trampoline (which assumes ordinary call/ret semantics at
// the target) will corrupt the stack if detoured onto one of these -- a real, live
// crash this project hit once already (known_issues_x64.md issue #4, the
// Hook_ConsoleFontInit crash, 2026-09-26).
//
// This script reads the UNWIND_INFO structure the matched RUNTIME_FUNCTION points
// at (Version/Flags byte, SizeOfProlog, CountOfUnwindCodes, FrameRegister/Offset)
// and reports the two real, decisive signals:
//   - SizeOfProlog == 0 with few/no unwind codes -> almost certainly a shared
//     epilogue/tail-fragment, NOT a safe hook target.
//   - UNW_FLAG_CHAININFO (bit 2 of Flags) set -> this unwind info is explicitly
//     CHAINED to a parent RUNTIME_FUNCTION (a real, standard PE mechanism for
//     "this range's unwind continues in that other range") -- another strong,
//     direct signal this address is a secondary/split fragment of a larger real
//     function, not its own independent entry.
// A real function safe to hook typically has a real, nonzero SizeOfProlog, a
// nonzero CountOfUnwindCodes describing real register-save operations, and the
// queried address should equal the RUNTIME_FUNCTION's own BeginAddress (if it's
// mid-range, it's not even a function start at all).
//
// Usage: -postScript UnwindInfoLookup.java <output_path> <addr1> [addr2] ...

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;

import java.io.FileWriter;
import java.io.PrintWriter;

public class UnwindInfoLookup extends GhidraScript {

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args == null || args.length < 2) {
            println("Usage: UnwindInfoLookup.java <output_path> <addr1> [addr2] ...");
            return;
        }
        String outPath = args[0];
        Memory mem = currentProgram.getMemory();
        MemoryBlock pdata = mem.getBlock(".pdata");
        if (pdata == null) {
            println("No .pdata block found -- not a real x64 PE, or the block wasn't imported.");
            return;
        }

        // Real image base (preferred, per this project's own kPreferredImageBase
        // convention) -- RUNTIME_FUNCTION's three fields are RVAs, relative to this.
        long imageBase = currentProgram.getImageBase().getOffset();

        try (PrintWriter w = new PrintWriter(new FileWriter(outPath))) {
            for (int i = 1; i < args.length; i++) {
                String hex = args[i];
                long queried = Long.parseUnsignedLong(hex, 16);
                w.println("================================================================");
                w.println("Queried address: 0x" + Long.toHexString(queried));

                Address pdataStart = pdata.getStart();
                Address pdataEnd = pdata.getEnd();
                long entrySize = 12;
                boolean found = false;

                for (Address cursor = pdataStart; cursor.compareTo(pdataEnd) < 0;
                     cursor = cursor.add(entrySize)) {
                    long beginRva = Integer.toUnsignedLong(mem.getInt(cursor));
                    long endRva = Integer.toUnsignedLong(mem.getInt(cursor.add(4)));
                    long unwindRva = Integer.toUnsignedLong(mem.getInt(cursor.add(8)));
                    if (beginRva == 0 && endRva == 0) continue; // padding/unused

                    long beginVa = imageBase + beginRva;
                    long endVa = imageBase + endRva;
                    if (queried >= beginVa && queried < endVa) {
                        found = true;
                        w.println("RUNTIME_FUNCTION @ pdata offset 0x" +
                            Long.toHexString(cursor.subtract(pdataStart)));
                        w.println("  BeginAddress = 0x" + Long.toHexString(beginVa) +
                            (queried == beginVa ? "  <-- queried address IS the real function start" :
                                "  <-- queried address is " + (queried - beginVa) +
                                " bytes INTO this range, NOT the start"));
                        w.println("  EndAddress   = 0x" + Long.toHexString(endVa) +
                            " (range size " + (endVa - beginVa) + " bytes)");

                        Address unwindAddr = currentProgram.getAddressFactory()
                            .getDefaultAddressSpace().getAddress(imageBase + unwindRva);
                        int versionFlags = mem.getByte(unwindAddr) & 0xFF;
                        int version = versionFlags & 0x7;
                        int flags = (versionFlags >> 3) & 0x1F;
                        int sizeOfProlog = mem.getByte(unwindAddr.add(1)) & 0xFF;
                        int countOfCodes = mem.getByte(unwindAddr.add(2)) & 0xFF;
                        int frameByte = mem.getByte(unwindAddr.add(3)) & 0xFF;
                        int frameRegister = frameByte & 0xF;
                        int frameOffset = (frameByte >> 4) & 0xF;

                        w.println("  UNWIND_INFO @ 0x" + Long.toHexString(unwindAddr.getOffset()));
                        w.println("    Version=" + version + " Flags=0x" + Integer.toHexString(flags) +
                            (( flags & 0x4) != 0 ? " [UNW_FLAG_CHAININFO -- this unwind info is CHAINED "
                                + "to a parent RUNTIME_FUNCTION, a real, strong signal this is a split "
                                + "fragment, not an independent function]" : ""));
                        w.println("    SizeOfProlog=" + sizeOfProlog +
                            (sizeOfProlog == 0 ? " [ZERO -- no real prologue: this range establishes no "
                                + "stack frame of its own, a strong signal it's a shared epilogue/tail "
                                + "fragment reached via JMP, not a real callable function -- DO NOT "
                                + "MinHook this address]" : ""));
                        w.println("    CountOfUnwindCodes=" + countOfCodes);
                        w.println("    FrameRegister=" + frameRegister + " FrameOffset=" + frameOffset);

                        String verdict;
                        if (queried != beginVa) {
                            verdict = "UNSAFE: queried address is not even the real function's own start -- "
                                + "it's a mid-range address (return-address-style), do not hook directly.";
                        } else if (sizeOfProlog == 0) {
                            verdict = "UNSAFE: SizeOfProlog is 0 at the real function start -- this function "
                                + "establishes no stack frame of its own (a shared epilogue/tail-fragment "
                                + "target). MinHook's trampoline will corrupt the stack here.";
                        } else if ((flags & 0x4) != 0) {
                            verdict = "UNSAFE: UNW_FLAG_CHAININFO set -- this is explicitly a secondary/split "
                                + "fragment of a larger function, not an independent one.";
                        } else {
                            verdict = "LIKELY SAFE: real function start, nonzero prologue, not chained -- "
                                + "a normal, independently hookable function by this specific check (still "
                                + "confirm via a raw disassembly/DumpSigBytes.java prologue read before "
                                + "actually hooking, per this project's own standing convention).";
                        }
                        w.println("  VERDICT: " + verdict);
                        break;
                    }
                }
                if (!found) {
                    w.println("No RUNTIME_FUNCTION entry covers this address -- either it's outside any "
                        + "unwind-tracked range (leaf-function-with-no-frame territory, rare on x64 but "
                        + "possible), or it's genuinely not real code.");
                }
                w.println();
            }
        }
        println("Wrote report to " + outPath);
    }
}
