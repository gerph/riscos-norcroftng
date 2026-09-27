# RISC OS relocatable modules (`-zM` / `-zps`)

Part of [riscos-n++](overview.md).

This document is **assessment only** — the user has already indicated
module-specific builds will likely not be supported by this compiler for
now, and asked specifically for notes on how they might be added later and
whether that's a large undertaking. Nothing here is being implemented.

## Decisions

### `-zM` and `-zps` are currently parsed but functionally inert

Traced through the actual option-handling code, not inferred from absence:

- `-zM` (module-code generation) is recognised: it's listed in
  `ncc/ccacorn/options.h`'s help text ("Generate code suitable for
  building a RISC OS relocatable module"), and the driver
  (`ncc/mip/driver.c`, the `-z` option switch) falls through to
  `mcdep_config_option()` in `ncc/arm/mcdep.c`, whose `case 'm':` stores
  a `-zm` tool-env value (a module level, defaulting to `1`). **Nothing
  else in the compiler reads that tool-env key** — it's stored and then
  never consulted again by codegen, the AOF writer, or anywhere else
  (confirmed by grepping the whole `ncc/`/`ncc-support/` tree for `"-zm"`
  outside `mcdep.c` itself: no hits). Passing `-zM` today changes nothing
  about the generated code or object file.
- `-zps<n>` (software stack-check suppression level) **is** genuinely
  implemented — this is a distinct, general codegen feature (whether
  function prologues emit a software check against the stack-limit
  register `R_SL`), not module-specific: `PCS_NOSTACKCHECK` threads through
  `ncc/arm/mcdpriv.h`, `ncc/mip/regalloc.c`, `ncc/arm/gen.c`, and is
  recorded as a real AOF area attribute (`AOF_NOSWSTKCK`) in
  `ncc/armthumb/aaof.c`. It matters *for* modules (modules can't rely on
  the stack-extension trampoline an ordinary application gets from
  `C:o.stubsG`/SharedCLibrary, so they need real software stack checking
  in every non-leaf function), but it works today independently of
  whether `-zM` does anything, and isn't itself the missing piece.

So: the option users would type is accepted without an error, which could
be mistaken for "it works" — it silently produces an ordinary application
object, not a module-safe one. That's a real trap for anyone using this
compiler who's used to `-zM` doing something.

### What "generate code suitable for a module" actually requires, that's
### currently missing

Based on how RISC OS relocatable modules work (per this build
environment's own RISC OS domain knowledge — not re-derived from
Norcroft's source, since the gap is in Norcroft, not in RISC OS itself):

1. **Forced software stack checking.** A module has no stack-extension
   trampoline; every function that might need more stack than is
   guaranteed must check against `R_SL` itself or risk corrupting whatever
   memory follows the RMA-allocated block. `-zps` already provides the
   mechanism; what's missing is `-zM` actually forcing it on (today `-zM`
   doesn't touch `PCS_NOSTACKCHECK` at all).
2. **No reliance on stub-provided startup/veneers.** A module's entry
   points (`_start`/init, `_final`/finalise, service calls, SWI dispatch)
   are called directly by the kernel with a specific register convention —
   there's no `main()`-style C startup coming from `C:o.stubsG`. Norcroft
   NG's C front end currently assumes an ordinary `main()`-based program
   (see the working `hello world` examples throughout this design); there
   is no module entry-point convention (title string, help string, SWI
   number-to-handler table, the `Module_*` veneers CMHG normally
   generates) anywhere in `ncc/` or `ncc-support/`.
3. **AOF module-specific area attributes.** `ncc/armthumb/aaof.c` writes
   ordinary AOF areas with attributes like `AOF_NOSWSTKCK`; there's no
   equivalent module-position-independence or RMA-target attribute being
   written, because nothing in the driver sets one.
4. **The CMHG boundary.** In this build environment today, module builds
   split cleanly: C source is compiled with module-flavoured codegen
   (`-zM -zps1` under Norcroft, or GCC's translated equivalent per
   `riscos-help howto-gcc`), while the module *header* (title/help/SWI
   table/veneers) is generated separately by `riscos-cmunge` from a CMHG
   file, and `riscos-link` combines the two. That division of labour
   wouldn't need to change — Norcroft NG's job would only be #1–#3 above,
   not reimplementing what CMHG already does.

### Feasibility estimate: real, non-trivial, but bounded — not a rewrite

This is not "the AOF writer doesn't support modules so we'd need a new
backend." The AOF writer, register allocator, and stack-check machinery
already exist and are already wired together (`-zps` proves the wiring
works). What's missing is:

- Making `-zM` actually flip `PCS_NOSTACKCHECK` off (and any other
  module-appropriate defaults) instead of being a no-op — likely a small,
  contained change in `ncc/arm/mcdep.c`/`ncc/mip/driver.c`.
- Deciding whether Norcroft NG's front end should ever need to know it's
  compiling towards a module at all, or whether "module-safe object code"
  is *entirely* achievable via already-working flags
  (`-zps0`/force-stack-check, plus whatever APCS variant a module needs)
  with **no CMHG-boundary work required in the compiler** — in which case
  most of the "gap" is actually just `-zM` needing to become an alias for
  the right combination of already-working flags, not new code generation.
  This second possibility is genuinely plausible given how thin the actual
  missing piece (#1 above) turned out to be on inspection, but hasn't been
  proven — it would need an actual attempt (build a trivial module by hand
  with `-zps0` and whatever APCS options seem right, run it through
  `riscos-cmunge`/`riscos-link`, and see what breaks) to know for sure.

Given that, "huge undertaking" looks like the wrong framing. A more
accurate one: **a small compiler change (making `-zM` do something) plus
an investigative spike (try building and running an actual module) would
likely tell you within a day or two of focused work whether this is
"basically works already" or "needs real new codegen."** The honest
answer right now is: **not verified either way** — this assessment
identifies the specific gap and a plausible path, but no module has
actually been built end-to-end with this compiler, so there could be a
harder problem hiding in area #2 or #3 above that only shows up once
something real is attempted.

## Open Questions

- Should the investigative spike described above (hand-build a trivial
  module, see what breaks) happen as a small follow-up task even though
  full module support isn't being built now? It would convert "plausible"
  above into an actual answer at fairly low cost, and would directly
  inform whether this stays a "future work" note or becomes a small
  near-term task. Not blocking anything in this design either way.

## Proposals

- Treat this document as the record of "why not now" for module support,
  and revisit it (rather than starting from scratch) whenever module
  support actually becomes a live priority — the specific gaps identified
  here (`-zM` inert, no module entry-point convention, unverified AOF
  attribute needs) should still be accurate unless the compiler changes
  substantially in the meantime.
