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
- [Testing and validation](testing-and-validation.md) — current test
  results, and how to validate ongoing work (including real execution, not
  just codegen assertions).

## Open Questions

<Cross-cutting questions spanning more than one area.>

- **How much should this design push into actual environment integration
  now vs. defer entirely?** This document currently defers all
  `/riscos-resources` changes to a later, explicitly-directed phase (see
  Scope above). [build-and-integration.md](build-and-integration.md) still
  specifies *what* that integration should look like, so that later phase
  has something concrete to implement from, rather than starting cold.
  Flagging this as an explicit choice: if you'd rather this design stopped
  short of specifying the integration shape at all, say so and that
  section can be trimmed to a one-line pointer instead.
- **Positioning relative to the existing `riscos-c++`** (2005 Acorn cfront
  translator): this design proposes pure coexistence — `riscos-n++` is a
  new, additional tool, and `riscos-c++` is untouched — since nothing in
  the request asked for a replacement and touching an existing, presumably
  still-used tool is a bigger and more consequential decision than adding
  a new one. If eventual replacement/deprecation of `riscos-c++` is
  actually the intent, that changes the framing of
  [build-and-integration.md](build-and-integration.md) from "add a tool"
  to "plan a migration," which is worth saying explicitly rather than
  assuming.
