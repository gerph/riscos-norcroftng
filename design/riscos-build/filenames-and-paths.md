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

- **Colon-path/system-variable expansion for `#include` search is *not*
  confirmed working**, and needs more tracing before it can be called
  done. What's established:
  - `ncc/mip/driver.c`'s `pathfromenv()` helper (reads a named environment
    variable, and — when `Compiling_On_Unix` — rewrites `:` to `,` inside
    its value) is verbatim-identical to the same function in `cc`'s own
    `driver.c`. This part is inherited unchanged, not reimplemented.
  - This build environment already exposes RISC-OS-style path variables
    as plain environment variables — confirmed: `C` is set to
    `/riscos-built/Export/Lib/CLib/,/riscos-built/Export/Lib/,...`,
    already comma-separated (RISC OS list style), not colon-separated.
    Whether `pathfromenv`'s colon-to-comma rewrite does anything harmful
    or useful against a value that's already comma-separated wasn't
    checked (it's likely a harmless no-op, since there's no `:` character
    to replace, but "likely" isn't "confirmed").
  - **What's not established**: whether/where the general `-I` mechanism
    actually invokes `pathfromenv`-style expansion for a colon-suffixed
    argument. Tracing `driver.c`'s `-I` handling shows `AddInclude()`
    simply stores the literal argument string — no colon detection there.
    A live test (`-IMYPATHVAR:` with `MYPATHVAR` set to a real directory
    containing the header) **failed** — `#include <myinc.h>` wasn't found.
    This could mean the mechanism lives elsewhere (not yet located — likely
    in `ncc/cfe/pp.c`'s actual file-open logic, where the
    "wouldn't open" error text originates) and needs a different
    invocation syntax than what was tried, or that this specific
    capability genuinely doesn't work yet in this fork. **Needs
    resolving before this requirement can be marked done** — this is the
    one piece of the user's stated requirement that isn't yet backed by
    either a working example or a precisely root-caused gap (unlike the
    `fname` duality above, which has both).
  - Note this is scoped to the *compiler's* include search specifically.
    The colon-path resolution already proven working in this design
    (`C:o.stubsG`, see [linking-and-c-library.md](linking-and-c-library.md))
    is `riscos-link` resolving a *linker* argument, a separate tool with
    its own (already-working, unexamined-here) path logic — it says
    nothing about whether the compiler's own `#include` search does the
    same thing correctly.

## Proposals

- Once the CLX `fname` swap is done, add tests exercising exactly the
  cases the user listed (`c.main`, `main.c` with a `c/main` fallback,
  `foo.h` with an `h/foo` fallback) as part of closing this area out —
  see [testing-and-validation.md](testing-and-validation.md). These
  weren't testable meaningfully before the swap, since the current
  implementation is known not to handle them.
