
Make template **instantiations** be:
1. **synthesised under their true (origin-absolute) name** — `::k::Optional<T>`
   stays conceptually in `::k::Optional`, not flattened into the consumer module;
2. **deduplicated cleanly across modules** via `linkonce_odr` + **COMDAT** (the
   C++ ODR model): every module that uses an instantiation emits its own copy,
   marked fusable; the linker keeps **one** (static link) or the dynamic linker
   interposes **one** (shared libs, default visibility).

This replaces the current fragile model (flatten-to-consumer-root + `ExternalLinkage`
+ a registry that unifies imported vs locally-synthesised `struct_type`s).

End state: imported KDIs carry the template **recipe** + instantiation **shape**
(origin + args + layout), **not** instantiation bodies; consumers always
re-synthesise; the `reassign_aggregate`/registry unification path is removed.

---

## 2. Baseline findings (verified in code)

| Aspect | Current state | File |
|---|---|---|
| Instantiation method linkage | `ExternalLinkage` | `gen/gen_function.cpp:885`, `:808` |
| Member ctor/dtor linkage | `ExternalLinkage` (only global init/fini are `Internal`) | `gen/gen_constructor.cpp:616,783` |
| Existing `linkonce_odr` use | typeinfo/dtor wrappers, intrinsics (no explicit Comdat) | `gen/gen_statements.cpp`, `gen/gen_intrinsics.cpp` |
| Mangling | FQN + `mangle_template_short_name` (deterministic, identical across modules **for the same name**) | `model/mangler.cpp:217-308` |
| KDI | instantiations **are** exported with `template_origin` | `model/tools/kdi_exporter.cpp:842-935` |
| Shared-lib build flags | `-shared -fPIC`, **no** `-fvisibility=hidden` → default visibility | `compiler_linker.cpp:243,320` |
| Origin tag (chantier-1) | `tpl_info::origin_module_ns_fq` + `unit::make_instantiation_registry_key()` | `model/template.hpp`, `model/model.cpp` |

### Two instantiation models (critical)

* **Generic (uniform synthesis)** — `template<typename T>` → `tpl_info::is_generic`.
  ONE type-erased (`byte*`-based) body shared by all args; **keeps the base name**
  `Optional` (no arg suffix); mangled **base-only** (`mangle_fq_name_template_base_only`).
  Path: `template_instantiator::synthesize_generic_aggregate`. `has_tpl_args()`
  is **false** on the synthesised aggregate.
* **Concrete per-arg** — e.g. `template<class T>` / value params → one body per arg
  set; name carries the arg suffix `Optional<int>`; mangled with args
  (`mangle_fq_name_templated`). Path: `template_instantiator::instantiate_aggregate`.
  `has_tpl_args()` is **true**.

⇒ Detection of "is an instantiation" via `has_tpl_args()` **misses the generic
case**. We add an explicit flag instead (see Phase 2).

### The module-nesting naming obstacle (critical)

`unit::set_unit_name()` assigns the **module name to the single root namespace**
(`get_root_namespace()->assign_name(::<module>)`). So **everything** a module
compiles lives under `::<module>` (e.g. consumer `foo` → imported `k` is
`::foo::k`). Consequences:

* A consumer-synthesised `Optional<int>` is currently homed under the *flattened*
  consumer root → mangles **without** the origin (`::Optional<int>`), or under
  `::foo::k::…` if re-homed — **neither equals libk's `::k::Optional<int>`**.
* For cross-module COMDAT dedup, the instantiation symbol **must be
  origin-absolute** (`::k::Optional<int>`), identical regardless of which module
  synthesises it. This requires giving the synthesised aggregate an
  **origin-absolute name** (decoupled from its model-tree position), built from
  `origin_module_ns_fq` (imported) or the template's enclosing ns (local).

⇒ **Naming (origin-absolute) and linkage (`linkonce_odr`) must land together**:
once two modules emit the *same* mangled name, `ExternalLinkage` would cause a
duplicate-symbol error; `linkonce_odr` makes them merge instead.

---

## 3. Locked design decisions

1. **Linkage**: `LinkOnceODRLinkage` + explicit **COMDAT** (group = mangled name,
   `Comdat::Any`) on every instantiation symbol: methods, ctors, dtors, vtable,
   RTTI, instantiation statics.
2. **Visibility**: **default** (no `hidden`) — required for cross-`.so`
   interposition so the diamond (A.so + B.so + C) unifies at load time.
3. **Naming**: instantiations get an **origin-absolute** name `::<origin>::Name…`,
   independent of the synthesising module's root prefix.
4. **KDI**: stop exporting instantiation **bodies**; export only the **recipe**
   (template) + **shape** (origin + args + layout). Consumers re-synthesise.
5. **Remove** the imported/local `struct_type` unification (`reassign_aggregate`,
   `_instantiation_struct_types` registry) once Phase 4 lands — a single module
   then has exactly one instantiation per (origin, args).
6. **Detection**: explicit `is_instantiation()` flag on `aggregate`/`function`
   (covers both generic and concrete models), set by `template_instantiator`.

### Diamond semantics (reference)

* **Static link** (A.o/B.o/.a into the exe): COMDAT keeps **one** copy →
  resolved at link edition. ✅
* **Shared libs** (A.so, B.so, C exe): each `.so` keeps its own copy (COMDAT is
  intra-link); unified at **load** by symbol interposition **iff default
  visibility** (K's case). ✅ With `hidden` it would diverge (RTTI identity break).
* Correctness conditions: ODR-identical recipe (same libk version), identical
  mangling, default visibility, full COMDAT group (body+vtable+RTTI+statics).

---

## 4. Phase plan (green at each boundary)

* **Phase 1 — Origin-absolute naming** ✅ **DONE (green)**: consumer-synthesised
  instantiations of *imported* templates are renamed to their origin-absolute
  name (`::k::Optional<byte>`), independent of the synthesising module, via
  `unit::make_origin_absolute_name()`. Sites: `gen/resolvers_aggregate.cpp` +
  `gen/resolvers_type_ref.cpp` (FQ-name assignment + unconditional child re-derive).
  Verified: a lib importing `k` emits `_KFMKN1k8OptionalIaE…` (= `::k::Optional<byte>`).
* **Phase 2 — Linkage infra** ✅ **DONE (green)**:
  - `is_instantiation()` flag on `aggregate`/`function` (`model_aggregate.hpp`,
    `model_function.hpp`), set by `template_instantiator` (generic + concrete +
    nested + free-function paths) and the inline member-template instantiation
    sites in `gen_expr_invocation.cpp` ;
  - `apply_instantiation_linkage()` helper (`gen/gen_helpers.hpp`) — linkonce_odr +
    COMDAT (group = mangled name), default visibility ;
  - wired into `declaration_generator::visit_function` (`gen_function.cpp:885`),
    which also covers ctors/dtors (they dispatch through `visit_function`).
  Verified: instantiation methods/ctors/dtors are `W` (weak) with COMDAT in the
  `.o`; non-instantiation functions stay strong `T`.
* **Phase 3 — Class-template vtable/RTTI** ✅ **DONE (green)**: vtable + RTTI globals
  (`_KTV` / `_KTRI`) of any template / generic / instantiation aggregate are
  `linkonce_odr` + COMDAT, gated by `should_merge_aggregate_symbols(klass)`
  (= `is_instantiation() || is_template()`) in `gen/gen_class.cpp` `visit_klass`
  (interfaces delegate to `visit_klass`). This covers both concrete instantiations
  (`::k::Optional<int>`) and the base-erased generic templates homed at root
  (`::Collection`, `::Vector`, …) that every consumer re-emits. The RTTI **reflection
  function descriptors** (`_KTRF`, member + free) are now `PrivateLinkage`
  (`gen_class.cpp` + `gen_unit.cpp`): they are referenced only by baked pointers in this
  module's RTTI (never by mangled name), so module-local linkage removes the
  duplicate-strong-symbol collisions (matches the already-private ctor/param descriptors).
  Verified: `_KTVN…` / `_KTRIN…` weak (`V`), `_KTRF…` local; static + shared diamonds link.
  **Remaining (minor, non-blocking):** secondary vtables (multiple inheritance) not yet
  COMDAT'd (no cross-module multiple-inheritance template test exercises them).
* **Phase 4 — KDI recipe+shape only** 🟡 **PARTIAL**: the **transitive-origin** half is
  done — the KDI carries a re-exported template's true origin module
  (`kdi_template_def.origin_module`, CBOR + JSON), the exporter fills it from
  `tpl_info::origin_module_ns_fq`, and both importer paths prefer it. **Still TODO**: stop
  exporting/importing instantiation **bodies**; remove the `reassign_aggregate` /
  `_instantiation_struct_types` registry unification; curb the consumer **over-emission** of
  RTTI for imported generic templates it never instantiates (now harmless — weak/private —
  but wasteful); re-validate `[bais]`.
* **Phase 5 — Dedup proof + diamond tests** ✅ **DONE (green)**: both the **shared-library**
  and the **static-archive** diamonds are proven for `::k::Optional<int>` and
  `::k::Expected<int,int>` — two libs A and B plus an exe C each instantiate the libk
  template; the diamond links cleanly and runs (exit 42) in both linkage modes. Tests:
  `[import][e2e][instantiation-diamond-shared]` (×2, shared, `test-import.cpp`) and
  `[klangc][instantiation-diamond-static]` (static, `test-klangc-static-diamond.cpp`).
* **Phase 6 — Docs & cleanup** ⏳ TODO: finish Phase 4 (KDI recipe-only + registry removal),
  then update `doc/spec` + `AGENTS.md` and delete this transient doc.

---

## 5. Risk register

| Risk | Sev | Mitigation |
|---|---|---|
| Naming change breaks resolution/KDI homing | High | Phase 1 atomic with Phase 2; keep tree-position vs name decoupling localised; full ctest each step |
| Mis-detecting instantiation symbols → wrong linkage | High | Explicit flag set at synthesis (covers generic + concrete) |
  **Generalised to USER-DECLARED (non-libk) templates** ✅: the dedup keys off the
  template's origin module (set by the KDI importer for *any* import) + the
  `is_instantiation()`/`is_template()` flags — never on libk — so a template `Box<T>`
  declared in a plain user library `boxlib` dedups identically across a diamond. Tests:
  `[import][e2e][instantiation-diamond-shared][user-template]` (shared, `test-import.cpp`)
  and `[klangc][instantiation-diamond-static][user-template]` (static,
  `test-klangc-static-diamond.cpp`). Each consumer synthesises its own
  `::boxlib::Box<int>` under the same origin-absolute name; both diamonds link and run
  (exit 42). **Observed (separate, minor):** the unused-import heuristic does not count a
  template *type* used only for instantiation (`b : boxlib::Box<int>;`), so it emits a
  false-positive `Warning 00008 (unused import 'boxlib')`. Harmless; tracked as a follow-up.
| Dual model (generic type-erased vs concrete) divergence | Med | Flag + tests for both; base-only vs arg mangling preserved |
| RTTI identity across `.so` | Med | default visibility; Phase 3 cross-module tests |
| ODR divergence (different libk versions) | Med | (optional) layout fingerprint in KDI (Phase 5) |
| Removing unification destabilises imports | Med | Phase 4 last; re-validate `[bais]`, import suite |

---

## 6. Test plan

* `[prod-lib][instantiation-comdat]`: 2 libs + exe all use `Optional<byte>` →
  `nm` shows weak/COMDAT, one surviving copy.
* `[instantiation-diamond-static]` / `[...-shared]`: diamond resolves (one copy).
* `[instantiation-diamond-vtable]`: class template, cross-module virtual dispatch
  + `dynamic_cast`.
* Generalised `[bais]`: cross-module return-by-value re-synthesis.
* De-skip `[.][import][template][homonym-imports]`.
* Full `ctest` (13 suites) at each phase boundary.

---

## 7. Progress log

* (init) Design locked; baseline findings captured. Starting Phase 2 (linkage infra).
* Phase 2 done & green: `is_instantiation()` flag + `apply_instantiation_linkage()`
  helper + wiring. Full ctest 13/13. `nm` shows instantiation methods/ctors/dtors
  as weak (`W`) + COMDAT; regular functions stay strong (`T`).
* Phase 1 done & green: origin-absolute naming for imported-template
  instantiations (`unit::make_origin_absolute_name`). Full ctest 13/13. Verified a
  lib importing `k` emits `::k::Optional<byte>` (`_KFMKN1k8OptionalIaE...`) weak.
* Phase 3 (primary) done & green: class/struct template instantiation **primary
  vtable + RTTI** are now `linkonce_odr` + COMDAT (`gen_class.cpp`). Full ctest
  13/13 (note: `ctest -j3` may spuriously time out `gen-oop` under VM load -- it
  passes standalone and under `-j2`). Verified `_KTVN...VBox__intE` / `_KTRIN...` weak.
* Recovered from a mid-edit crash: repaired the broken `materialise_template_def`
  (missing closing braces), removed a stray `[DBG-EXPORT]` `fprintf`, and
  de-duplicated a `kdi_json.cpp` line. Full ctest back to 13/13.
* Phase 4 (transitive-origin half) done & green: added `kdi_template_def.origin_module`
  (libkdi struct + CBOR + JSON encode/decode), exporter fills it from
  `tpl_info::origin_module_ns_fq`, both importer tagging paths prefer it.
* Phase 5 (shared diamond) done & green: verified end-to-end with `klangc` (libs A+B+exe C
  all instantiate `::k::Optional<int>` / `::k::Expected<int,int>`, `.so` diamond links and
  exits 42; instantiation symbols are weak `W` with origin-absolute mangled names). Added
  regression tests `[import][e2e][instantiation-diamond-shared]` (x2). Static-archive diamond
  documented as a skip (see Section 8).
* Phase 5 (static diamond) + Phase 3 follow-ups done & green: template/generic/instantiation
  vtable+RTTI (`_KTV`/`_KTRI`) -> `linkonce_odr`+COMDAT via `should_merge_aggregate_symbols`
  (`is_instantiation() || is_template()`); RTTI reflection function descriptors (`_KTRF`,
  member + free) -> `PrivateLinkage`. The static-archive diamond now links and runs (exit 42).
  New CLI regression test `[klangc][instantiation-diamond-static]` (`test-klangc-static-diamond.cpp`);
  the `test-import.cpp` skip placeholder was replaced by a pointer note. Full ctest 13/13.

## 8. Open items / gaps discovered

* **Transitive origin** -- **ADDRESSED (Phase 4 half)**: the KDI now records a
  re-exported template's true origin module in `kdi_template_def.origin_module`; the
  importer prefers it when tagging `tpl_info::origin_module_ns_fq`, so a module B that
  imports A (which re-exports `k::Optional`) now synthesises `::k::Optional<...>` (matching
  the direct importer) instead of `::optlib::Optional<...>`. Cross-transitive dedup of the
  *shared-library* diamond therefore holds. (Full transitive *static* dedup is still blocked
  by the generic-RTTI item below.)
* **Static-link diamond / base-erased generic RTTI** -- **RESOLVED**: a consumer of a
  libk template re-emits that template's RTTI/vtable/reflection descriptors (a lib using
  only `Optional<int>` still emits ~70 of them for Collection/Vector/LinkedList). These
  used to be strong, so a static link of two such archives collided. Now: vtable+RTTI
  (`_KTV`/`_KTRI`) are `linkonce_odr`+COMDAT for any template/generic/instantiation
  aggregate (`should_merge_aggregate_symbols` in `gen_class.cpp` `visit_klass`), and the
  `_KTRF` reflection function descriptors are `PrivateLinkage` (referenced only by baked
  pointers, never by name). Both the shared and static diamonds link and run (exit 42).
  Tests: `[import][e2e][instantiation-diamond-shared]`, `[klangc][instantiation-diamond-static]`.
  **Residual (minor):** the over-emission itself remains (now harmless weak/private bloat);
  removing it belongs to the Phase 4 KDI recipe-only rework.
* **Pre-existing limitation (unrelated)**: `Optional<byte>` rvalue copy-init
  (`o : Optional<byte> = f();` where `f()` returns by value) is rejected by ctor
  overload resolution (rvalue->`&` binding). Tracked separately (commit `6f2ca5f`).





