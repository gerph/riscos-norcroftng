# Linking and the C library

Part of [riscos-n++](overview.md). See
[build-and-integration.md](build-and-integration.md) for the build itself.

## Decisions

### "StubsG" is provided by the environment, not by this project

`StubsG` refers to `C:o.stubsG`, an object already present in this build
environment's RISC OS library search path (the `C:` path variable), used
exactly as this environment's own documentation describes
(`riscos-help build-and-link`): it's linked into a program in place of the
plain `stubs` object, and provides veneers that locate whichever
SharedCLibrary/C library implementation is actually available at run time,
rather than hard-wiring the program to one specific library build. This is
**not something this project needs to build, source, or vendor** — it's
infrastructure the environment already provides for every compiler that
targets 32-bit RISC OS.

Confirmed by actually linking and running a Norcroft-NG-compiled object
against it (see [overview.md](overview.md) for the transcript). No manual
step, no patching, no additional glue code was required for either a plain
C program or a small C++ program with a class.

This directly contradicts the starting assumption ("this may have to be
done manually") — worth flagging clearly since it changes what "done"
looks like for this whole project: the C-library-linking problem is
already solved by the environment, and this project's job is just to keep
emitting AOF objects whose calling convention and symbol references match
what `C:o.stubsG` expects (APCS argument passing, standard libc symbol
names) — which it already does, for the cases tested.

### `stubs.a` / `stubs-26.a` already in this repo are unrelated pre-existing
### static libraries, not "StubsG"

`lib/stubs.a` and `lib/stubs-26.a` (added in commit `d5da061`, "Added RISC
OS stubs to lib.") are real ALF-format static libraries already checked
into this repository, used today only to link the `HOST=riscos`
self-hosted compiler build (see the top-level `Makefile`'s
`STUBS_LIB`/`LDLIBS` handling under `ifeq ($(HOST),riscos)`). They are a
different, older stubs style (closer to the plain `C:o.stubs` this
environment's docs describe for 26-bit/static linking) and are unrelated
to `C:o.stubsG`. Nothing here needs to change for the cross-compiler path
this design focuses on; noting the distinction only so a future reader
doesn't conflate the two names.

### A related project (`pwombwell/aof-toolchain`) builds its own stub
### libraries instead of relying on an environment-provided StubsG

For context (not something to copy): that project fetches ROOL's
RISC_OSLib sources at build time (not vendored) and builds three separate
stub-library flavours itself — `stubs.a` (APCS-32, FPA/FPE3),
`stubs-26.a` (APCS-R/26-bit), and `stubs-vfp.a` (APCS-32, VFP hardfloat) —
because it targets environments that don't already provide an equivalent
of this environment's `C:o.stubsG`. That's not our situation: this
environment already provides a working, floating-point-ABI-agnostic
`C:o.stubsG` (see [floating-point.md](floating-point.md) — both an
FPA-ABI and a VFP-ABI program linked against the *same* `C:o.stubsG` and
both ran correctly). Building our own stub libraries would be duplicating
something the environment already provides correctly.

## Open Questions

- The smoke tests so far are simple (a `printf` call, a float add-and-print,
  a class with a constructor). `C:o.stubsG`'s actual implementation and
  exactly which library symbols it resolves at runtime hasn't been
  inspected directly — only exercised black-box. If a program needs a
  libc symbol Norcroft NG doesn't emit under the name/convention
  `C:o.stubsG` expects (unusual name mangling, an unsupported calling
  convention edge case), that would only surface with broader testing. See
  [testing-and-validation.md](testing-and-validation.md) for the plan to
  broaden coverage rather than treating the current smoke tests as proof
  of full compatibility.
- No investigation has been done yet into whether C++ features needing
  runtime support beyond plain function calls (exceptions, RTTI,
  `operator new`/`delete` needing a heap) work against `C:o.stubsG` as-is.
  The tested C++ example used neither. This matters once real C++ code
  (not just language-feature tests) is compiled, and is worth a dedicated
  test pass before calling C++ support "usable" rather than "compiles."
