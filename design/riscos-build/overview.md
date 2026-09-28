# riscos-n++: Norcroft NG as a native/cross RISC OS compiler toolchain

## Goal

Make this repository (Norcroft NG) buildable and usable inside the RISC OS
build environment as a supported compiler, primarily as a **Linux-hosted
cross-compiler**, installed and invoked the same way the environment's other
compilers are (`riscos-cc`, `riscos-cc-via-gcc`, `riscos64-gcc`). The
headline deliverable is a C++ cross-compiler, `riscos-n++`, giving the
environment a real, integrated C++ front end for the first time (the
existing `riscos-c++` is a 2005 Acorn "C++ Language System" cfront-style
translator to C, unrelated to this codebase). A companion C compiler,
`riscos-ncc`, falls out of the same build as a near-free byproduct.

Building *on* RISC OS (self-hosting, `HOST=riscos`) is explicitly
lower-priority — the repository already has a bootstrap path for it
(cross-build a compiler, then use it to rebuild itself natively), but this
design does not focus effort there.

## Scope

**In scope:**
- Confirming and documenting how Norcroft NG cross-builds on Linux today,
  and what output format/ABI it targets.
- How the resulting `ncc-riscos`/`n++-riscos` binaries link against the
  RISC OS C library (the "StubsG" question) using tools already present in
  this build environment.
- A design for installing these binaries into the environment as
  `riscos-ncc` / `riscos-n++`, consistent with existing naming and
  wrapper conventions.
- The floating-point ABI question (FPA vs VFP vs soft-float) — current
  state, what's tested, and a recommendation.
- Assessing RISC OS relocatable-module support (`-zM`/`-zps`): current
  state and a feasibility/effort estimate for adding it later. This is
  **not** being implemented now.
- A validation/testing plan, building on the existing `runtests.py` suite
  and this environment's `riscos-run`/Pyromaniac execution tooling.

**Out of scope (for this design pass):**
- Actually installing anything into `/riscos-resources` (the shared
  build-environment tool directory) or editing the shared AMU makefiles
  (`TOOLCHAIN32`-style hookup). That is real, consequential change to
  infrastructure shared by every project in this environment, and per this
  environment's standing rules is a separate, explicitly-directed
  implementation task, done only after this design is reviewed.
- An AOF-format library archiver / `ar` equivalent — not needed, see
  [build-and-integration.md](build-and-integration.md); `riscos-libfile`
  already exists in the environment.
- A GCCSDK-derived assembler/linker (`asasm`/`drlink`) — not needed, and
  not wanted: it's GPL-licensed, which this project's licensing rules
  forbid using. See [build-and-integration.md](build-and-integration.md).
- Building a soft-float (no-FPU) runtime library. Noted as a real gap in
  [floating-point.md](floating-point.md) but not designed here.
- Implementing `-zM`/`-zps` module codegen. Assessed only, in
  [relocatable-modules.md](relocatable-modules.md).
- A C++ standard library. The user has confirmed none exists yet and none
  is being designed here; `riscos-n++` is a language-level C++ compiler
  only for now (see [build-and-integration.md](build-and-integration.md)).

## Key finding that reshapes this design

The premise going into this ("we don't have AOF output, ELF would be
sufficient, StubsG linking may have to be done manually") turned out to be
wrong in an important way, **confirmed by actually building and running
code**, not just reading source:

- `make ncc TARGET=riscos` and `make n++ TARGET=riscos` both build cleanly
  on this Linux host with only the system `cc`.
- The resulting `bin/ncc-riscos` and `bin/n++-riscos` already emit valid
  AOF objects (`ncc/armthumb/aaof.c` is a real, working AOF writer — this
  is not a stub).
- Those AOF objects link successfully against this build environment's
  existing `C:o.stubsG`, using the existing `riscos-link` tool — no new
  tool, library, or manual step was needed.
- The linked binaries **ran correctly** under `riscos-run` (Pyromaniac):
  a plain C `printf` program, and a small C++ program using a class with a
  constructor and a member function, both produced correct output.

So AOF output and StubsG linking are not gaps to design around — they are
already working. ELF output is consequently **not needed** and is dropped
from scope; see [build-and-integration.md](build-and-integration.md) for
the evidence and reasoning. This freed up the effort that would have gone
into an ELF backend, which is why floating-point ABI and module support get
deeper treatment below instead.

## A second finding, less welcome: virtual functions crashed at runtime
## (now fixed)

Hand-testing beyond the existing `tests/cpp` suite (which had no coverage
of virtual functions, `new`/`delete`, or exceptions) found that **any
class with a virtual function crashed immediately at runtime**, in the
smallest possible reproduction (one virtual method, no inheritance, called
directly on a concrete stack object — not even through a pointer). Root
cause: `ncc/ccacorn/options.h` (used for `TARGET=riscos`) was missing a
`TARGET_VTAB_ELTSIZE` define that the other C++-capable targets all have,
which caused an unrelated code-size-padding directive to unconditionally
bypass the compiler's otherwise-correct vtable-content logic, always
emitting a branch instruction where the call site expected a data
pointer. **Fixed** with a one-line define, verified against several
polymorphism cases (through a pointer, through `new`, through a virtual
destructor), with a regression test added
(`tests/cpp/virtual/vtable_single_method_is_data.cpp`) and no regressions
in the rest of the suite. Full write-up in
[build-and-integration.md](build-and-integration.md).

Two further, separate C++ front-end crashes were found alongside it while
testing. The first — overloading `operator new[]`/`operator delete[]`
aborted the compiler with an internal consistency-check failure — is also
now **fixed** (same pattern: a parser gap the code's own comment already
flagged, fixed in `cppfe/xsyn.c`; see
[linking-and-c-library.md](linking-and-c-library.md) for the write-up and
a real, separate gap it surfaced — `new T[n]` for a type with a
non-trivial destructor needs a runtime helper no C++ library provides
yet, which is a library gap, not a compiler bug). The second — compiling
`throw` segfaults the compiler outright — was investigated in depth (see
[build-and-integration.md](build-and-integration.md)): unlike the other
two, it isn't a small, localised bug. `try`/`catch` builds an
exception-dispatch jopcode (`J_TYPECASE`) structurally similar to the
ones already used for vtables and `switch` statements, but several places
that special-case those (`mip/csescan.c`'s CSE pass, and critically the
ARM backend's own code generator, which has a literal no-op for it) were
never extended to handle `J_TYPECASE` too — a feature that was started
(parsing, AST, dispatch-table construction) and never finished (CSE
awareness, actual type-dispatch codegen, likely a runtime type-matching
helper). Genuinely implementing it is a real feature project, not
attempted here; instead, confirmed with the user to take the safe option
— `try`/`catch`/`throw` now fail immediately with a clean compile-time
error (`Fatal error: 'try-catch' unimplemented`) instead of eventually
segfaulting deep inside CSE.

With the virtual-function crash fixed, the `operator new[]`/`operator
delete[]` crash fixed, and `throw` now failing cleanly instead of
crashing, `riscos-n++`'s honest current scope is "compiles polymorphic
C++ including array new/delete for trivial-destructor types, minus a
standard library, minus exceptions (which it now refuses clearly rather
than silently mishandling)" — all three C++ crashes found during this
design pass are now resolved, two by fixing the underlying bug and one by
converting an unsafe crash into a safe, clear rejection.

## Areas

- [Build and integration](build-and-integration.md) — how the cross-build
  works today, what changes to make it produce `riscos-ncc`/`riscos-n++`,
  and how that relates to existing tools in the environment.
- [Linking and the C library](linking-and-c-library.md) — the StubsG
  finding in detail, what "the C library" means for a Norcroft NG binary,
  and what isn't covered (no libstdc++ equivalent).
- [Floating point](floating-point.md) — FPA vs VFP vs soft-float: what's
  implemented, what's tested, what's genuinely unresolved.
- [Relocatable modules](relocatable-modules.md) — current state of
  `-zM`/`-zps`, and a feasibility assessment for later.
- [Filenames and paths](filenames-and-paths.md) — RISC OS/POSIX filename
  duality (`c.main`/`main.c`/`c/main`) and colon-path/system-variable
  handling for includes: a real, user-confirmed requirement, root-caused
  to a specific gap.
- [Testing and validation](testing-and-validation.md) — current test
  results, and how to validate ongoing work (including real execution, not
  just codegen assertions).

## Open Questions

<Cross-cutting questions spanning more than one area.>

(None currently cross-cutting — the two open questions previously here
are resolved, see Decisions below. Remaining open questions are all
area-specific; see each area document.)

## Decisions

- **Environment integration: export only, mirroring `cc`'s own pattern.**
  Confirmed by the user: "we just export the build and let the integration
  tooling handle the location for installation." This repository builds
  and names its own output; a separate, already-existing mechanism (the
  `resources.yaml` manifest convention used by `cc` and other tools in
  `native-build-tools`) is how those outputs get picked up and placed into
  `/riscos-resources` — this repository does not do that placement itself,
  and this design does not need to specify where things ultimately land.
  See [build-and-integration.md](build-and-integration.md) for the
  concrete `resources.yaml` shape this implies.
- **Coexistence with `riscos-c++` confirmed.** Confirmed by the user: "yes
  we will coexist with CFront." `riscos-n++` is a new, additional tool;
  `riscos-c++` (the 2005 Acorn cfront-style translator) is untouched, with
  no migration/replacement implied.
