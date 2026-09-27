# Filenames and paths

Part of [riscos-n++](overview.md).

The user's requirement, stated directly: *"Does the tooling accept RISCOS
format filenames? That's a requirement. If c.main is given it must use
c/main on posix systems. And if main.c is given it must look at c/main as
well if the literal name isn't present. Similarly for includes, foo.h
should also check h/foo... includes should handle RISCOS colon paths and
variables as cc does."*

## Decisions

### Root cause found: `ncc-support/fname.c` is a known-incomplete
### reimplementation, not real CLX — and the call sites are already correct

Traced concretely, not inferred:

- `ncc/mip/driver.c` and `ncc/mip/compiler.c` already call
  `fname_parse()`/`fname_unparse()` at exactly the same call sites, with
  the same arguments, as the reference `cc` tool's own `driver.c`/
  `compiler.c` (`/riscos-source/native-build-tools/.../Tools/cc/
  crosscompile/{driver,compiler}.c`) — confirmed by direct comparison,
  same line content, same `FNAME_SUFFIXES`/`FNAME_INCLUDE_SUFFIXES`
  constants. **Nothing needs to change in the compiler's own call sites.**
- What differs is the implementation behind those calls.
  `ncc-support/fname.c`/`fname.h` in this repository is a from-scratch
  reimplementation (Copyright 2025 Piers Wombwell, Apache-2.0) — its own
  header comment admits the gap directly: *"For RISC OS, this is joyous
  fun. The file extension is placed before the leafname, and I haven't
  figured out when we need to change slashes to dots."* That's precisely
  the `foo.c <-> c.foo <-> c/foo` extension-inversion duality the user is
  asking for.
- The real CLX `fname` module (found in this environment at
  `/riscos-source/native-build-tools/riscos-source/Sources/Lib/CLX/{c,h}/
  fname`, Copyright Advanced RISC Machines Ltd., 1992) documents exactly
  this feature as its purpose: *"to support the use of multiple
  file-naming conventions on one host... Note that, under RISC OS,
  name.<extn> is recognised as a RISC OS name. Extension inversion is
  performed... only for a specified list of extensions"* (the `suffixes`
  parameter — this is `FNAME_SUFFIXES`/`FNAME_INCLUDE_SUFFIXES`, already
  passed correctly by Norcroft NG's driver/compiler code).
- **Reproduced the failure live**, consistent with this diagnosis:
  compiling a file passed as `c/main` (POSIX-rendered RISC OS name) with
  the current cross-built `ncc-riscos` gives
  `Error: type of 'c/main' unknown (file ignored)` — the current
  `ncc-support/fname.c` doesn't recognise it. Compiling the same file as
  `main.c` (plain POSIX name) works today, but that only exercises the
  trivial case; nothing was found or tested that makes `c.main`,
  `c/main`, or the `foo.h`/`h/foo` include case work with the current
  reimplementation.

### Proposed fix: swap in real CLX's `fname` module

Replace `ncc-support/fname.c` and `ncc-support/fname.h` with the real CLX
`c/fname` and `h/fname` sources (adjusted only as needed for this
project's build — e.g. header guards/include paths — not behaviourally).
No changes to `ncc/mip/driver.c` or `ncc/mip/compiler.c` are expected to be
needed, since their calls already match what `cc` itself does against the
same module. This directly satisfies the stated requirement per the
user's own framing — *"these are handled by CLX; any failures in that
regard are CLX bugs"* — by actually using CLX for it, rather than
maintaining a second, admittedly-incomplete implementation alongside it.

### A licensing fact worth recording plainly

CLX's `fname.h`/`pathmacro.h` (and, presumably, its other modules) carry
explicit copyright notices — "Advanced RISC Machines Ltd., 1992" and
"Pace Micro Technology plc., 2000" — with **no LICENSE file found**
anywhere in the CLX source tree, and no license header in the files
themselves beyond the bare copyright line. Under this project's licensing
rules (no GPL; otherwise MIT/Apache-2.0 as used elsewhere in this repo),
an unlicensed proprietary-copyright dependency would normally be a real
stop-and-check moment. Recording the mitigating context rather than
treating it as a fresh risk this project is introducing: CLX is already
pervasive, load-bearing infrastructure across this entire build
environment — it's exported by `native-build-tools`/`native-build-headers`
and consumed by dozens of existing projects under `/riscos-source`
(`riscos-gos`, `riscos-gcontext`, `pyromaniac`, `justin/p2cc`, and others),
and it's exactly what the reference `cc` tool this design was told to
mirror already depends on for this very feature. This isn't a new
acceptance decision Norcroft NG is making alone; it's adopting an
already-standing one. Flagging it here anyway, in case that standing
decision is itself something you'd want revisited — that's a call above
this design's scope, not something to silently assume is fine.

## Open Questions

- **Colon-path/system-variable expansion for `#include` search: tested
  directly against the environment's real, production `riscos-cc`
  (v5.18) — not just Norcroft NG — and it did not work, in a cleanly
  isolated test.** The user confirmed the intended syntax
  (`-IC:` to search every directory listed in variable `C`; `-I<Lib$Dir>.
  GetOptDir` to substitute `Lib$Dir`'s value, matched host-side by a
  `LIB_DIR` environment variable, then append `.GetOptDir`) is correct.
  Isolated the test carefully to rule out confounds:
  - Confirmed `LIB_DIR` and `C` are both real, already-set environment
    variables in this container (`LIB_DIR=/riscos-built/Export/Lib`,
    `C=/riscos-built/Export/Lib/CLib/,/riscos-built/Export/Lib/,...`,
    already RISC-OS-style comma-separated).
  - Confirmed a plain literal `-I/absolute/path` works correctly against
    `riscos-cc` (isolates that basic `-I` parsing and file-open both work
    at all).
  - With that same working setup, replacing the literal path with either
    `-IC:` or `-I"<Lib\$Dir>.GetOptDir"` (both with and without a space
    after `-I`) **consistently failed** to find a header
    (`marker.h`/`h/marker`, both forms present) that the literal-path form
    found without issue.
  - `riscos-cc -help` documents only the plain form
    (`-I<directory>   Include <directory> on the #include search path`) —
    silent on both variable forms, which is consistent with either "not
    implemented" or "real but undocumented," so the help text doesn't
    settle it either way.
  - Source-level: neither `cc`'s own `compiler.c`/`pp.c` nor Norcroft NG's
    equivalents call CLX's `pathmacro_resolve` (the function that actually
    implements `<Var>` bracket substitution — read its full source at
    `Sources/Lib/CLX/c/pathmacro`) anywhere. `cc` opens include files with
    plain `fopen()`. This is consistent with the empirical failure: the
    code path that would need to exist to make either syntax work doesn't
    appear to be wired into the compiler's own file-open logic at all, in
    either codebase.
  - **This raises a real fork needing your call, not mine to guess**:
    is this expansion actually meant to happen a level up — in `riscos-amu`
    (the Makefile tool), which would expand `<Lib$Dir>`/`Foo:` in Makefile
    text before ever constructing the command line the compiler sees — so
    that the compiler binary itself never needs this logic, and Norcroft
    NG already gets it "for free" the moment it's driven through
    `riscos-amu` rather than invoked directly (matching how I tested it,
    bypassing AMU entirely)? Or is direct command-line expansion inside
    the compiler genuinely expected to work (in which case it doesn't work
    even in the existing production `riscos-cc` today, which would be
    worth knowing regardless of this project). I don't have a way to tell
    these apart from here without either reading `riscos-amu`'s own source
    or you confirming which layer is supposed to own this.

## Proposals

- Once the CLX `fname` swap is done, add tests exercising exactly the
  cases the user listed (`c.main`, `main.c` with a `c/main` fallback,
  `foo.h` with an `h/foo` fallback) as part of closing this area out —
  see [testing-and-validation.md](testing-and-validation.md). These
  weren't testable meaningfully before the swap, since the current
  implementation is known not to handle them.
