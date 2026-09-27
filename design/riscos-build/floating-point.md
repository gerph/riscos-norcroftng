# Floating point: FPA vs VFP vs soft-float

Part of [riscos-n++](overview.md).

The user's own framing going in: *"I'm unsure whether we support VFP only,
or FPA instruction sets for floating point (and whether we support the
soft-float libraries - we don't have that yet)."* This document answers
that from the actual source and test suite, rather than guessing.

## Decisions

### Both FPA and VFP are real, substantially-implemented, tested code
### generation paths — this is not an open architectural question

Confirmed by reading the source and running the existing test suite
against the cross-built compiler, not just by the presence of files:

- `-apcs 3/32/fpe3` (FPA, via the FP Emulator ABI) and `-apcs 3/32/vfp`
  (VFP hardfloat) are both recognised, real keywords in the driver's APCS
  option table (`ncc/arm/mcdep.c`, `pcs_keywords[]`), alongside `fpe2`.
- Both have real codegen consumers across `ncc/mip/cg.c`,
  `ncc/mip/flowgraf.c`, `ncc/mip/regalloc.c`, `ncc/arm/mcdep.c`,
  `ncc/arm/gen.c`, plus disassembler support in `ncc-support/disass-fpa.c`,
  `ncc-support/disass-vfp.c`, `ncc-support/vfp.c` — this is not a partial
  stub for one path and a real implementation for the other.
- Running `tests/fpa` and `tests/vfp` (parallel, near-identical test sets —
  same test names, one ABI each) against the cross-built `bin/ncc-riscos`:
  - `tests/fpa`: **30/30 pass.**
  - `tests/vfp`: **29/30 pass, 1 failure** — see Open Questions.
- Directly linking and running both an FPA-ABI and a VFP-ABI build of the
  same small floating-point program against the *same* `C:o.stubsG`, under
  `riscos-run` (Pyromaniac): both produced the correct result
  (`5.750000`). See [linking-and-c-library.md](linking-and-c-library.md)
  for why this matters — the environment's C library linking doesn't force
  a choice between the two.

VFP support is newer (git history: "Added initial VFP support" is a
recent commit, versus FPA/FPE3 support going back to the original Acorn
codebase), but "initial" undersells it — it passes 29 of 30 codegen tests
today.

### No soft-float (no-FPU) support exists, and none is being built here

Confirmed: there is no `tests/softfp`-equivalent directory, no
`PCS_SOFTFP`-style flag, and no runtime floating-point emulation library
anywhere in `external/`, `lib/`, or `ncc-support/`. Both FPA and VFP paths
assume real floating-point hardware (or, for FPA, the FP Emulator module
providing hardware-equivalent semantics) — there is no path that avoids
floating-point instructions entirely and calls out to a software emulation
library instead. This matches the user's own understanding; recorded here
as a confirmed fact rather than a guess, and left out of scope (see
[overview.md](overview.md)).

## Open Questions

- **The one VFP test failure**, `tests/vfp/apcs/abi_basics-15.c`: expected
  a `vcvt.f64.f32 d2, s4` and it wasn't emitted — a real, specific bug in
  VFP argument-marshalling codegen for a mixed float/double APCS call
  case, not a fundamental gap. Worth fixing before calling VFP "done," but
  it's a bounded, findable bug, not evidence VFP codegen is broadly
  unreliable (see the other 29 passes).
- **Varargs marshalling between the two ABIs**: tested directly, following
  through on the concern a related project raised (see below) rather than
  leaving it as a guess. Wrote a variadic function mixing `int`, `double`,
  and `float` arguments read via `va_arg`, and a direct `printf("%d %f %d
  %f\n", ...)` call mixing int and float literals, and ran both under both
  `-apcs .../fpe3` and `-apcs .../vfp`, linked against `C:o.stubsG`, under
  `riscos-run`. **Both ABIs produced correct results in every case
  tried**, including through `printf` itself (not just our own
  `va_arg`-based function). `pwombwell/aof-toolchain` found it necessary to
  write a hand-rolled `printf` wrapper specifically to convert varargs
  between VFP and FPA calling conventions — that trouble didn't reproduce
  here, at least for these cases; this environment's `C:o.stubsG`/
  SharedCLibrary appears to already handle both ABIs' varargs marshalling
  correctly for `printf`. This isn't exhaustive (only `int`/`float`/
  `double` combinations up to 4 arguments were tried, not structs-by-value
  or every function in the standard library), but the specific risk raised
  didn't materialise, and there's no longer a specific reason to expect it
  will elsewhere.
  (Note while testing this: a first attempt using C99-style mid-block
  variable declarations failed to compile under both ABIs identically,
  with `Serious error: <command> expected but found 'int'` — that's
  correct, expected behaviour for a strict ANSI C89 compiler like this
  one, not a bug; declarations must lead a block. Mentioning it only so a
  future reader hitting the same error doesn't mistake it for another
  compiler crash — the three genuine ones are catalogued in
  [linking-and-c-library.md](linking-and-c-library.md) and
  [build-and-integration.md](build-and-integration.md).)
- **No decision has been made on a default** ABI when a project doesn't
  specify one explicitly. This environment's own documentation
  (`riscos-help build-and-link`) uses `-apcs 3/32/fpe3` in its 32-bit
  example, suggesting FPA/FPE3 may be the environment's own established
  convention today — but that doc predates this project and isn't
  necessarily a mandate. See Proposal below.
- **Whether mixing FPA-ABI and VFP-ABI objects in one link should be
  actively prevented.** Right now nothing stops it, and the two ABIs pass
  floating-point values completely differently (FPA registers vs VFP
  registers) — linking an FPA-compiled object with a VFP-compiled one that
  both touch the same floating-point call boundary would silently produce
  wrong results rather than a link error, unless the objects happen not to
  call into each other's floating-point-touching functions.
  `pwombwell/aof-toolchain`'s answer was to mechanically rename FPA-only
  CLib veneer symbols with a `__scl_` prefix in the VFP build so an
  accidental link against the wrong flavour fails with an undefined-symbol
  error instead of corrupting the stack. Whether an equivalent guard is
  worth building here (and if so, where — this would need to live in
  whatever the eventual RISC-OS-side stub/library story is, not in the
  compiler itself) is unresolved and not blocking anything else in this
  design; it only becomes urgent once real multi-object-file RISC OS
  projects start choosing between the two ABIs.

## Decisions (continued)

### Default to FPA/FPE3

Confirmed by the user: default to FPA "for maximum compatibility" when a
project doesn't specify an ABI explicitly. This also matches this
environment's own existing documented convention
(`riscos-help build-and-link`, which uses `-apcs 3/32/fpe3` in its 32-bit
example) and the fact that FPA has zero known codegen test failures today
versus VFP's one (see Decisions above). Both ABIs remain fully available —
this only sets what happens when a project's makefile doesn't ask for
either explicitly (via `-apcs .../fpe3` or `-apcs .../vfp`, exactly as
today).

## Proposals

- **Support both FPA and VFP as explicit, user-selected options**, rather
  than picking one and dropping the other. Both are real and largely
  working; dropping either would be throwing away working code generation
  for no proven benefit. (This part remains a Proposal in the sense that
  it hasn't been separately asked; it's implied by "default to FPA" above
  meaning VFP stays available, not removed.)
- **Soft-float stays fully out of scope** until someone has an actual
  no-FPU-hardware target that needs it — building a software floating
  point emulation library is a substantial undertaking with essentially no
  current demand signal (RISC OS 5's supported hardware all has an FPU or
  FPE), so speculative work here isn't proposed.
