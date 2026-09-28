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

## Decisions (continued)

### `new`/`delete`/exceptions/RTTI: tested directly, results vary from
### "needs a two-line shim" to "actively unsafe to use"

Each tested in isolation against `n++-riscos` + `C:o.stubsG`:

- **`operator new`/`operator delete` (non-array forms)**: not provided by
  `C:o.stubsG` (expected — it's a C library stub, not a C++ runtime).
  Compiling `new Widget(42)` without them fails to *link*, with clean,
  ordinary undefined-symbol errors (`__nw__FUi`, `__dl__FPv`). Supplying a
  two-line user definition —
  `void *operator new(unsigned int size) { return malloc(size); }` and the
  `delete` equivalent — compiles and links cleanly. **This specific gap is
  genuinely small**: a minimal C++ support library providing just these
  two functions (forwarding to the C library's `malloc`/`free` through
  `C:o.stubsG`) would cover it, not a large undertaking.
- **`operator new[]`/`operator delete[]` (array forms)**: **fixed** —
  overloading these used to crash the compiler itself (a parser bug
  misparsed `operator new[]`'s `[]` as an array declarator rather than
  part of the operator name, ending in an internal consistency-check
  abort). Root-caused to `rd_operator_name()` (`cppfe/xsyn.c`) never
  checking for a following `[` `]` — its own comment already flagged the
  gap ("need to parse 'operator new[]' and 'operator delete[]' here").
  Fixed by peeking for the brackets and naming the declaration
  `__nw_v`/`__dl_v`, matching what the compiler's own array-new/delete
  codegen already calls. See
  [build-and-integration.md](build-and-integration.md) for the full
  write-up and the regression test
  (`tests/cpp/operators/new_delete_array_forms.cpp`).

  **A second, deeper gap surfaced while fixing this, and remains open**:
  `new T[n]` only calls the simple `__nw_v(size)` helper (the one a
  two-argument `operator new[]` override can satisfy) when `T`'s
  destructor is trivial — confirmed with a plain `int[]` and a
  no-user-declared-ctor/dtor struct, both linking and running correctly
  against a hand-written `operator new[]`. For a type *with* a
  user-declared destructor (like the `Widget` example used to find the
  original crash), the compiler instead calls a differently-mangled,
  three-argument helper (`__nw_v__FPvUiT2PFPv_v` — the extra parameter is
  a destructor-callback, for cleaning up already-constructed elements if
  a later element's constructor throws). No runtime library provides
  that helper, and a plain `operator new[](size_t)` override can't satisfy
  it either — its mangled name is different. This is best understood as
  a specific, concrete detail of the already-documented "no C++ standard
  library" gap (see [overview.md](overview.md)), not a new compiler bug:
  whoever eventually builds a C++ runtime for this project needs to
  provide that three-argument vector-new/vector-delete helper, wrapping
  the user's plain `operator new[]`/`operator delete[]` and looping the
  destructor callback over already-constructed elements on failure.
- **Exceptions (`throw`/`try`/`catch`)**: the compiler **segfaults**
  compiling a plain `try { throw 42; } catch (int e) {}`, after first
  emitting `Warning: Functionality of C++ keyword may not yet be fully
  implemented: 'throw'`. Not a graceful rejection — a crash. **`throw` is
  not just unsupported, it's currently unsafe to write in any code this
  compiler will see.** Investigated in detail (see
  [build-and-integration.md](build-and-integration.md) for the full
  write-up) — this one is architecturally different from the other two
  fixed bugs, not a candidate for the same kind of small, localised fix.
- **RTTI (`typeid`)**: fails cleanly (ordinary compile errors, no crash) —
  `<typeinfo>` doesn't exist and `type_info` is unresolved. Same category
  as the missing standard library generally: absent, but safe.

None of this is about `C:o.stubsG` specifically — once compilation
succeeds, linking against it has worked in every case tried. These are
compiler-side gaps (two are outright crashes), not C-library gaps. See
[build-and-integration.md](build-and-integration.md) for the related, more
fundamental finding that virtual functions crash at runtime unconditionally
— a compiler code-generation bug, more urgent than any of the above.

## Open Questions

- The smoke tests so far (this document's included) are still narrow.
  `C:o.stubsG`'s actual implementation and exactly which library symbols
  it resolves at runtime hasn't been inspected directly — only exercised
  black-box. If a program needs a libc symbol Norcroft NG doesn't emit
  under the name/convention `C:o.stubsG` expects (unusual name mangling,
  an unsupported calling convention edge case), that would only surface
  with broader testing. See
  [testing-and-validation.md](testing-and-validation.md) for the plan to
  broaden coverage rather than treating the current smoke tests as proof
  of full compatibility.
