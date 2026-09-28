# RISC OS relocatable modules (`-zM` / `-zps`)

Part of [riscos-n++](overview.md).

This document was originally **assessment only** — the user asked for
notes on how `-zM` might be added later and whether that's a large
undertaking, without building it. A follow-up investigation, prompted by
the user directly correcting a wrong guess (see below), found and
verified the actual mechanism, changing the answer from "not verified
either way, could be a day or two spike" to "confirmed, and it's one
line." Nothing has been made permanent yet — the verification was done
with a temporary change, tested, then reverted — pending the user
deciding whether to finalise it.

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

### Corrected: the real data-access mechanism is `arthur_module`, not the
### reentrant/static-base APCS variant — found by getting it wrong first

An earlier pass through this investigation found that `-apcs .../reent`
(a static-base register, `r9`, indirecting through a based-address table)
was fully implemented and assumed *that* was the mechanism modules would
need for "changes the way that data regions are accessed" — plausible,
since it's a real, working, relevant-sounding piece of codegen. **The user
corrected this directly**, from memory of real module disassembly: the
actual mechanism loads a new base from an offset from `SL` (`R10`, the
stack-limit register, repurposed here) and uses that as a delta, with the
offset itself being a linker-resolved symbol, not a literal number. That
pointed at a completely different, previously-unnoticed code path:

`ncc/arm/gen.c` (~line 1858) has a block gated on a global int,
**`arthur_module`** (RISC OS's original development codename) — not
`PCS_REENTRANT` at all:

```c
if (arthur_module) {
    ExtRef *x = symext_(name);
    if (x && !(x->extflags & xr_code)) {
        arthur_module_relocation();
        outinstr(OP_LDR | F_DOWN | F_RD(R_IP) | F_RN(R_SL));
        outinstr(OP_ADDR | F_RD(r1) | F_RN(R_IP) | r1);
        DestroyIP();
    }
}
```

This loads through `R_SL` (with a linker-patched negative offset — the
`F_DOWN` addressing mode, resolved via `arthur_module_relocation()` in
`gen.c` ~line 990, which imports either the symbol `_Mod$Reloc$Off` or
`_Lib$Reloc$Off` depending on the module vs. "library callable by a
module" variant), adds that to the link-time literal address of the data
item, and uses the result — a genuine runtime relocation-delta, exactly
matching the user's description and exactly what a module needs: its data
area's real address isn't known until the RMA allocates it, so every
static data reference needs this correction.

This is a real, multi-layered mechanism, not a stub: register allocation
is aware of it (`arm/mcdep.c` ~1192, marks `R_IP` live for `J_ADCON` under
`arthur_module`), and it has its own semantic check
(`mip/codebuf.c` ~443: `arthur_module` mode rejects taking the address of
one static and storing it directly in another, with a proper error,
`vargen_rerr_datadata_reloc` — a real restriction module code needs, since
such a reference can't be delta-corrected the same way).

**It's exactly as disconnected from `-zM` as everything else was**:
`arthur_module` is unconditionally set to `0` in `config_init()`
(`arm/mcdep.c` ~528) and *nothing else in the entire tree ever sets it to
anything else* — confirmed by grep. The `arthur_module == 1` / `== 2`
branches throughout `gen.c`/`mcdep.c`/`codebuf.c` are genuine, reachable,
correct-looking code that is simply never reached.

### Verified: the fix is one line

`config_init()` already reads other `-z<letter>` options the same way
(`integer_load_max = TE_Integer(t, "-zi", INTEGER_LOAD_MAX_DEFAULT);`).
Changing

```c
arthur_module = 0;
```

to

```c
arthur_module = TE_Integer(t, "-zm", 0);
```

exactly matches the existing `-zM`/`-zM1` convention already documented
in `ccacorn/options.h`'s help text (`mcdep_config_option`'s `case 'm':`
computes level `1` for bare `-zM`, `2` for `-zM1` — i.e. `arthur_module`'s
two branches directly, module vs. library-callable-by-a-module).

Tested with this one-line change, then reverted (nothing committed):

- `-zM` on a global-variable-touching function produces exactly the
  expected delta-relocation sequence, importing `_Mod$Reloc$Off`.
- `-zM1` produces the same shape, importing `_Lib$Reloc$Off` instead —
  confirming both existing branches are reachable and correct via this
  one change.
- Full existing test suite (`tests/c`, `tests/fpa`, `tests/vfp`) shows
  **no regressions** (63 passed / 1 known pre-existing VFP failure,
  unchanged) — this option is additive, doesn't disturb anything else.

### What's still open

- **`_Mod$Reloc$Off`/`_Lib$Reloc$Off` are not defined anywhere in this
  build environment's installed libraries** — grepped `/riscos-resources`
  and `native-build-tools`; the only hit is the reference `cc` tool's own
  matching source (confirming Norcroft NG faithfully preserved the same
  mechanism, not that the symbol is available). Whether `riscos-link`
  synthesises these symbols itself when linking module-type output,
  whether they come from a module-header veneer `riscos-cmunge` generates,
  or whether something else needs to provide them, is **unverified** —
  this determines whether a real module actually links and runs, as
  distinct from whether the compiler's codegen is correct in isolation
  (which is now confirmed).
- Point 1 from the original assessment above (forced software stack
  checking) is still accurate and still needed alongside this — `-zM`
  should presumably also force `PCS_NOSTACKCHECK` off, the same way it
  now needs to set `arthur_module`. Not yet combined/tested together.
- The CMHG boundary (point 4 above) is unaffected by this finding and
  still holds: this is only ever the compiler's C-source-to-object-code
  job; the module header stays `riscos-cmunge`'s.

## Open Questions

- Given the compiler-side fix is now confirmed small and correct in
  isolation, the natural next step is the investigative spike already
  proposed below (hand-build a trivial module, see if `riscos-cmunge`/
  `riscos-link` actually resolve `_Mod$Reloc$Off` and produce a module
  that runs) — this is now much more likely to be worth doing soon, since
  the main unknown left is entirely on the linking/tooling side rather
  than the compiler side.

## Proposals

- Make the one-line `config_init()` change permanent, combine it with
  forcing `PCS_NOSTACKCHECK` off under `-zM`, and add a regression test
  (codegen-level, matching `-zM`'s expected `_Mod$Reloc$Off` import and
  delta sequence) — pending the user's go-ahead, since this crosses from
  "investigate" into "implement," which this session has consistently
  treated as a separate decision.

## Made permanent and tested (commit `161b7a8`)

Both halves of the fix are committed to `ncc/arm/mcdep.c` on `riscos-build`,
each with a regression test in `tests/c/modules/`, each confirmed to
actually fail with the fix reverted before being restored:

- `tests/c/modules/zm_data_relocation.c` — `-zM` alone, checks for the
  `_Mod$Reloc$Off` import and the `ldr ip, [r10]` / `add r0, ip, r0` delta
  sequence around a global-variable reference. Also confirmed by hand that
  `-zM1` imports `_Lib$Reloc$Off` instead (both branches reachable and
  correct via the one `TE_Integer(t, "-zm", 0)` change), though only the
  `-zM` case has a permanent regression test — `-zM1` differs only in which
  symbol name is imported, so this was judged adequately covered rather
  than needing a second near-duplicate test.
- `tests/c/modules/zm_forces_stackcheck_despite_noswst.c` — `-zM` combined
  with an explicit `-apcs.../noswst`, checks the stack check (`cmp ip,
  r10` / `blmi __rt_stkovf_split_big`) still appears despite the user's
  request to suppress it.

**Correction found while building these tests**: the original write-up
above slightly overstated what needed testing. Software stack checking is
already on by default for any large-enough stack frame (confirmed: a
completely plain compile, no `-zM`/`-zps`/`-apcs` at all, already emits the
check for a 1024-byte frame) — `-zps1` is also already a baked-in default
`DRIVER_OPTIONS` entry for `TARGET=riscos` (`ccacorn/options.h`), not
something a user needs to pass. So the `PCS_NOSTACKCHECK`-clearing half of
this fix isn't needed to get a check to appear at all in the common case —
it specifically matters only when a user explicitly overrides with
`-apcs.../noswst`, which is what `zm_forces_stackcheck_despite_noswst.c`
actually exercises (confirmed: with the fix reverted, that combination
produces zero stack-check instructions; with it restored, the check
reappears).

Full existing suite re-run after landing: 79 passed / 1 failed (the same
pre-existing, known VFP failure noted in
[testing-and-validation.md](testing-and-validation.md) — no regressions).

**Still open, unchanged from above**: whether `riscos-link` actually
resolves `_Mod$Reloc$Off`/`_Lib$Reloc$Off` when linking a real module is
still unverified — this only confirms the compiler's codegen is correct in
isolation. That's the next step.

## End-to-end verified: a real module builds, links, loads, and runs

Hand-built a trivial module (`riscos-project create --type cmodule
--skeleton`, one added global `int init_count` incremented in `Mod_Init`),
compiled with `bin/ncc-riscos -zM` (not the installed `riscos-cc`), the
CMHG header built normally with `riscos-cmunge -32bit`, and linked with an
entirely ordinary `riscos-link -rmf -rescan -C++ -o rm32/ZMTest
oz32/modhead oz32/module C:o.stubsGS`:

- **The link succeeds with no undefined-symbol error for
  `_Mod$Reloc$Off`**, despite nothing in this build ever defining it —
  confirming `riscos-link` synthesises the symbol itself for `-rmf`
  (relocatable-module-format) output, resolving the open question above.
- `riscos-module-parser` confirms a structurally valid 32-bit-safe module
  (`Title: ZMTest`, `Version: 0.01`).
- **Actually run under Pyromaniac** (`riscos-run "RMLoad rm32.ZMTest"`):
  printed `Module ZMTest initialised (count=1)` — the RMA-relocated global
  was correctly written and read back at its real runtime address, proving
  the whole delta-relocation chain (codegen, link-time symbol resolution,
  and the RMA's actual runtime placement) works correctly together, not
  just in isolated codegen inspection.

This closes the last open item in this document: `-zM` module code
generation is implemented, tested, and verified working end-to-end.
