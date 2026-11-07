---
name: model-maker-component-development
description: "Use when adding a Tekla-style connection to model-maker."
version: 1.0.0
author: Hermes Agent
license: MIT
platforms: [linux, windows]
metadata:
  hermes:
    tags: [C++, CAD, OpenCASCADE, Tekla, parametric-component]
    related_skills: [model-maker, tekla-open-api-tooling, structural-design-calculations]
prerequisites:
  commands: [git, g++, cmake, ninja]
---

# model-maker component development (Tekla-style parametric connections)

## When to Use

- Building a steel connection (End Plate, Fin Plate, Base Plate, Gusset, Splice, …) in
  `/home/ubuntu/model-maker-overnight` so a future session can add a NEW connection type
  without rewriting the CAD mechanics (plate solid, BoltGroup, hole/fitting modifier, weld,
  EntityId, ownership, undo/redo, MMW8, recompute).
- Given a Tekla documentation page for a component and asked to adapt it.

Diyalog/önizleme paneline dokunuyorsan önce `references/gui-dialog-smoke-tests.md`: offscreen
test kuralları, Win32 sözdizimi kapısı, aynı türden ikinci tablo tuzağı, checks[] ret görünürlüğü.
Bağlantı VARSAYILANLARI/checks/kapsam testi/golden disiplini için `references/defaults-checks-and-coverage.md`.

## Source of truth — do NOT rewrite these

These systems already exist and are the real engines. New work COMPLETES them; it never
re-creates them in parallel.

- `component_engine.hpp/.cpp` — typed param schema (`CompParamSpec`/`CompParamValue` variant),
  `ComponentRegistry` (factory, register by TypeId — no switch/case in Document/renderer/MMW8),
  `GenerationPlan` (`PlateSpec`/`BoltGroup`/`CutModifier`/`WeldSpec`), `ComponentInstanceRecord`.
- `component_recompute.hpp/.cpp` — `ComponentRefs`, `ComponentDependencyGraph`, `RecomputePlanner`,
  `atomicRecompute` (prepare-then-commit, all-or-nothing).
- `constraint_system.hpp/.cpp` — `HandleKey` + `DistanceConstraint`, rank-based position solver.
- `magnetic_frame.hpp/.cpp` — `ComponentLocalFrame` + `LocalPlacement` (rigid child frames).
- `geometry_reference_resolver.hpp/.cpp` — `GeometryReference` (EntityId + semantic role, no
  transient TopoDS_Face index in persistent refs).
- `component_framework.hpp/.cpp` — `ComponentObjectSpec` (roleKey → typed object),
  `ComponentRelationshipGraph` (model, never a solver), `ComponentBuilder` (fluent API),
  `ComponentChildRecord` + `ComponentChildTable` (type-safe, deterministic roleKey→EntityId),
  `ComponentPlan` (roleKey-preserving; carries the FULL relationship graph),
  `componentPlanFromObjects` / `componentPlanFromGenerationPlan`.
- `component_materializer.hpp/.cpp` (MM_HAS_OCC) — CONCRETE materializer, not an interface:
  `prepare` (generate + validate OCCT geometry, no Document mutation) / `commit` / `recompute`
  into Document + OccSolidStore. Plate/BoltGroup/Cut AND Weld supported — Weld produces a REAL
  fillet solid (`buildWeldSolids`); a pair that does not touch keeps the record with
  `geometrySupported=false` + `unsupportedReason` (never a silent success).
  roleKey→EntityId persists via the MMW8 'N' block and recompute reuses the SAME EntityId
  (replaceModel / setBoltGroup, never addModel / addBoltGroup).

A new component = `registerType(typeId, generator)` + a generator that fills a `GenerationPlan`.
The GUI path (`applyEndPlateFromParts`) and the generic `generateEndPlate` are currently TWO
separate paths — converging them is tracked debt, not a reason to write a third path.

- **`ComponentMaterializer::commit` does NOT copy plan parameters into the record.** It stores
  `ComponentInstanceRecord.params` exactly as the caller set it. A caller that builds a record with
  only `typeId`/`mainPart`/`secondaryParts` and commits gets correct geometry but an EMPTY param set:
  the Properties panel shows nothing and recompute has no inputs — a silent data loss that no
  geometry test catches. Always fill `rec.params` (aligned to the type schema) BEFORE `commit`;
  assert it in the creation test (`plateThickness`/`boltCount` lookups on the stored record).

## Pipeline order (every component type)

Definition → Resolve Inputs → Validate Params → Resolve Geometry Refs → Solve Constraints →
Build GenerationPlan → Validate Geometry → Materialize OCCT → Atomic Document Commit →
Recompute/Undo/Redo/MMW8.

- Fail-closed: unresolved reference / ambiguous contact → explicit status, do NOT fabricate a
  plate "anyway". A failed prepare must leave the document unchanged (atomicRecompute).
  **Ret mesajı TEŞHİS taşır, yoksa kullanıcı "neden reddedildi" diye sorar.** İyi bir fail-closed
  mesajı üç parçadır: (1) NEDEN (durum koduna göre ayrı metin: paralel eksen / yüz dışı / değmiyor /
  belirsiz / çözülemedi), (2) SAYI (hangi üye/parça, nokta koordinatı, aday yüzler ve mm uzaklıkları,
  en yakın yüz ve yorumu — "kiriş ucu kolonun TEPESİNE oturuyor, yan başlığa temas yok"),
  (3) ÖNERİ ("köşe düğümü için Haunch (40) kullanın"). Türkçe karakterleri ASCII'ye kırpma
  (çözülemedi/yerleştirilemedi/yüzeyi/geçersiz) — kullanıcı bunu ayrıca şikâyet eder. Mesajı
  zenginleştirmek geometriyi DEĞİŞTİRMEZ: davranış aynı kalır, yalnız tanı okunur olur; bunu testte
  "hâlâ fail-closed" iddiasıyla birlikte koru.
- Unknown component type on load → keep existing geometry (orphan flag), never delete it.

## roleKey conventions

- roleKey is a STABLE string (`main_plate`, `beam_bolts`, `top_stiffener`, …); roleKey → EntityId
  must stay stable across recompute and MMW8 reload.
- Reserved roles: `main`, `secondary_0..N` = USER parts (referenced, never owned);
  `component` = the component instance itself (the only valid `ownedBy` target).
- `ownedBy(child, target)` with target `main`/`secondary_N` is REJECTED — a component owns its
  own children, it does not own the user's column/beam. `cuts`/`welds` MAY target user parts.
- Generators assign SEMANTIC roleKeys, never positional ones. `GenerationPlan` carries
  `plateKeys`/`boltGroupKeys`/`cutKeys`/`weldKeys` parallel to the spec vectors; the generator
  names each child (`fin_plate`, `beam_bolts`, `end_plate`). `componentPlanFromGenerationPlan`
  uses the semantic key, falls back to positional `plate_N` only when the key is absent, and
  REJECTS duplicate or non-`[A-Za-z0-9_]` keys, AND rejects reserved roleKeys (`main`,
  `secondary_N`) as child identity — a child named `main` collides with the user-part role and
  creates ambiguity. Positional keys break child identity the moment
  a generator reorders its own output — semantic keys keep roleKey→EntityId stable under
  reordering.

## Pitfalls (each cost real time — read before coding)

- **Every geometry-mutation path is a recompute trigger; enumerate them, don't trust one.**
  `recomputeComponentInstancesFor` can be perfectly correct and the component still goes stale,
  because a DIFFERENT edit path mutates the member without calling it. Known triggers:
  `commitTransformPoint` (classic/linear move) and `gumballEndDrag` recompute; the member-END GRIP
  drag (`reExtrudeProfileGrip`) re-extrudes the profile but recomputed only cut TARGETS — so moving
  a column fixed the connection while dragging the beam end left the plate and bolts where they
  were. Tell-tale: ONE member updates the connection and another does not → the fault is a missing
  call site, not the graph; do not touch `ComponentDependencyGraph` (verify it separately with a
  portable probe linking `libmodel_maker_core.a`, `dependentsOf({mainPart})` / `({secondaryPart})`
  must return the instance). Fix = call the same gated pair (`if (recomputeEndPlateConnectionsFor(ids))
  recomputeComponentInstancesFor(ids);`) inside the SAME open undo record before the path ends, and
  add new paths (props-panel reprofile, level change, future edits) to that list.
- **When the behaviour does not fire on the user's machine, publish the branch instead of guessing.**
  After wiring a trigger that should visibly recompute, write the outcome to `solidStatusMessage_`
  ("bagli instance yok" vs "N baglanti yenilendi") plus `invalidateCanvas()`. One status-line report
  from the user tells you which branch ran; without it every hypothesis costs a Windows round-trip
  (rebuild + scp + manual test). Silent success is the worst state: "nothing happened" and
  "recomputed" are indistinguishable.
- **Parameter change "doesn't take effect": trace the FULL chain before patching.**
  dialog → callback → setSelected → prepare/commit → recomputeCutTarget → base-version → render.
  Symptom that betrays the real cause: some DEPENDENT geometry moves (bolt grip shifts) but the
  target object does not → the target is being re-tessellated from a STALE source, not failing to
  update. In model-maker this was `OccSolidStore`: edits wrote the ACTIVE version but
  `recomputeCutTarget` re-cut from the BASE version, snapping the plate back to the old size.
  Fix = sync the base version to the active solid after an edit, inside the same undo record.
- **Disjoint/grazing boolean segfaults in Extrema_ExtCC on Windows OCC.** A hole/cut cylinder that
  is disjoint from, or just grazes the edge of, its target produces a degenerate boolean that
  segfaults (classic `Extrema_ExtCC::Perform` backtrace). Center finite hole cylinders on the grip
  center, size them to fully pierce the material stack, and add a bbox-disjoint guard to
  `booleanCutShape` (skip the boolean if tool and target bboxes are disjoint). The SAME rule hits
  FUSE too, not only cut: fusing two DISJOINT solids segfaults on Windows OCCT 7.9.3 while Linux
  7.9.2 silently returns a compound (no crash) — so it reads as a "flaky Windows-only" crash but is
  DETERMINISTIC (same step, same shape, every run). Classic trigger: a round-trip test's "fuse with
  probe" uses a FIXED-origin box (e.g. `MakeBox(0.5,0.5,0.5)`); for a CUT shape (cube minus centre
  cylinder) that box lands entirely inside the central void → two disjoint solids. Prove it with
  `BRepExtrema_DistShapeShape` (distance > 0), not by eyeballing positions. Fix the TEST, not the
  engine: make the probe a box that fully ENCLOSES the shape's bbox (bbox ± margin) so it always
  genuinely intersects. This crash is NOT the parallel-boolean race —
  `BOPAlgo_Options::SetParallelMode(false)` does not fix it; read the crashing test's actual probe
  geometry before applying a commit-history-analogy guard.
- **Repo lives in a MEGA sync folder → ninja says "no work to do" despite changed sources.** MEGA
  rewrites file mtimes, so ninja thinks nothing changed. After `git pull`, `touch` the changed
  `.cpp` files and build with `--clean-first`; pause MEGA sync while building/testing. The same
  symptom appears on Linux after an ordinary edit: the build prints only `Linking CXX executable`
  and a newly added `printf`/status string never appears — `touch` the source and rebuild before
  trusting that test output.
- **Never tell the user a CMake option/flag exists without grepping `CMakeLists.txt` for it, and
  point Windows users at their EXISTING configured `build-occ` (its cache already holds
  `OCC_ROOT`/`MM_OCC_LINK_DIRECT`).** Configuring a fresh `build/` with only `CMAKE_PREFIX_PATH`
  takes the `find_package(OpenCASCADE)` path and dies on missing `VTK::` targets; made-up flags
  (`OCC_USE_VTK`) cost several round-trips.
- **A bundle merge that adds NEW source files (or edits CMakeLists.txt, or adds a `.qrc`) needs a reconfigure before `cmake --build`.** Ninja's build graph is fixed at configure time; `.cpp` files newly added to a library target are invisible until `cmake -S . -B <builddir>` re-runs, so a `cmake --build` after merging a bundle reports `ninja: no work to do` (or rebuilds only the one already-known file) and the user believes the change is already compiled. Diagnose with `ls -la builddir/CMakeFiles/<lib>.dir/<newfile>.cpp.obj <newfile>.cpp`: obj missing/older than source = stale graph (reconfigure); obj newer = genuinely up to date.
 **Daha hizli teshis: yeni kaynagin adini build grafiginde ara.** `grep -c <yeni_kaynak> <builddir>/build.ninja` = 0 ise grafik BAYAT (dosya hic bilinmiyor); `-rw-r--r-- <builddir>/build.ninja` tarihi `CMakeLists.txt`'ten ESKI ise de ayni sey. `ninja: no work to do` mesajina GUVENME: CMake yeniden kosmadan mevcut hedefi raporlar. Zorla: `cmake -S . -B <builddir>` (ayni cache/opsiyonlarla) sonra `cmake --build <builddir> -j3`. Coklu build dizini varsa (`build-contract` contract testleri, `build-occ-hermes` ana suite) HER IKISINI yeniden yapilandir — yalniz birinde yesil gormek digerini kapsamaz.
- **A diyagonal/brace beam crossing a column**: the entry face is found by ray-casting from the
  CONNECTED end toward the column centre, not from the far end (far-end ray hits the back face and
  the fitting cuts the wrong side). Keep the beam's anchor line, project only along the axis.

- **RelationshipGraph is a MODEL, never a solver.** Do not re-implement cycle or conflict
  detection that `ConstraintSystem` already owns. Translate only the true solve-dependency
  edges (`AttachedTo`/`DependsOn`) into `addDependency` and let `solve()` report `Cyclic`; a
  hand-rolled DFS or a parallel/perpendicular-conflict scan is a parallel engine that review
  flags. Ownership edges (`OwnedBy`/`Cuts`/`Welds`) are lifecycle, not solve-dependencies —
  routing them through `addDependency` fabricates `Cyclic` results.
- **A framework that only runs in its own unit test is PARALLEL, not integrated.** The acceptance
  gate for a new component layer is materialization into `Document`/`OccSolidStore` plus
  recompute wiring — not a green test. Until that slice lands, say "not yet wired into the
  pipeline" rather than calling it complete; an independent reviewer will mark it "parallel" on
  exactly this. Geometric relationships (Coincident/Offset/Align/FaceContact) become
  `DistanceConstraint`s in `ComponentMaterializer::prepare` via an injected
  `GeometryReferenceResolver` that resolves the target `main`/`secondary_N` roleKey to a
  `ReferencePlane`; a missing resolver or an unresolved face returns explicit `Unresolved` (never
  silent success). `Parallel`/`Perpendicular`/`FollowFrame` have no orientation solver — return
  explicit `Unsupported`.
- **Move+recompute must be ONE undo step: the materializer shares the caller's transaction.**
  `ComponentMaterializer::commit()/recompute()` take an `openOwnRecord` flag (default true).
  Standalone create/edit (`applyFinPlateFromParts`, `setSelectedComponentParams`) use the default
  true — the materializer opens its OWN `pushSnapshot` and self-rolls-back on failure (one command
  = one undo record). But the move/reprofile/gumball triggers ALREADY opened a `pushUndoSnapshot`;
  there the recompute path (`recomputeComponentInstancesFor`) must pass `openOwnRecord=false` so the
  materializer writes into the SAME open record and does NOT `pushSnapshot`/self-rollback. If it
  opened a second record, move+recompute become TWO undo steps (diverging from End Plate) and a
  multi-component failure's `rollbackOpenRecord` rolls back the WRONG (top) record, leaving the move
  applied but components stale. This is the `commitEndPlateConnection` pattern (writes into the open
  record, never pushes its own) generalized to the materializer. Wrap the flag in a lambda
  (`auto rollback = [&]{ if (openOwnRecord) doc_.rollbackOpenRecord(); };`) and call it on every
  error path. TRAP: `replace_all` of `doc_.rollbackOpenRecord()` → `rollback()` also rewrites the
  lambda BODY (infinite recursion) — after the replace, fix the two lambda definitions by hand.
- **Load/open recompute runs with NO open record — open your own transaction.**
  `Document::load`/`commitFromMmw8` CLEARS undo/redo history, so a recompute that passes
  `openOwnRecord=false` (relying on the caller's record) writes its mutations into a nonexistent
  record and, on failure, `rollbackOpenRecord()` returns false — partial writes stay. On the
  load/open path, `pushSnapshot()` BEFORE the recompute loop and `clearHistory()` after success
  (loading must not be undoable, matching the End Plate load path's semantics). The same
  `openOwnRecord=false` flag is correct on move/reprofile/gumball because those ALREADY opened a
  snapshot; it is WRONG on load because load just wiped the stack.
- **Two recompute paths sharing one transaction must be GATED, not sequenced.**
  When a single trigger (move/reprofile/gumball) recomputes BOTH the legacy End Plate path and
  the parametric component path, the first must return a success bool and the second must run
  only `if (first(ids)) second(ids);`. If the first fails it rolls back the WHOLE move record
  (undoing the move itself), so the second would then write against an already-rolled-back state
  with no open record — or worse, call `rollbackOpenRecord()` again and pop the wrong record.
  Gate, don't just call both in sequence.
- **Component recompute must refresh the cut base like End Plate does.**
  `recomputeComponentInstancesFor` must call `rebuildCutBaseSolid(changedParts)` before prepare,
  exactly as `recomputeEndPlateConnectionsFor` does. The materializer re-applies bolt cuts from
  the stored `baseVersion`; a part moved by move/gumball has a STALE base (the store still holds
  the pre-move solid), so without the rebuild the re-cut snaps geometry back to the old position.
  Applies especially to a part attached ONLY to the component (End Plate has no connection there,
  so its own rebuild never runs for it).
- **A new verb-noun pending command must be wired into EVERY command-start/close path.**
  `startFinPlateCommand` sets a `pendingFinPlate_` flag and left/right-click/Esc hooks consume it;
  but if `startTransformCommand`, `selectTool` and `deactivateAllCommands` don't ALSO call
  `cancelPendingFinPlateCommand()` first, the pending flag survives starting a move or drawing tool
  and the NEXT left/right click is routed to the stale Fin Plate picker instead of the new command.
  Cross-cancel in all three entry points (mirror `cancelPendingCutCommand`'s placement), and add
  the pending flag to the hover branch (`pendingCutKind_ || pendingBolt_ || pendingFinPlate_`),
  the `refreshHoverCandidate` guard, and the `updateStatus` status-text branch — otherwise the
  pending command gets no hover highlight and no status line while it is active.
- **Materializer commit order: kind order is a HARD constraint, not a tie-break.**
  `commit`/`recompute` accumulate `out.ownedPlateIds` in iteration order, then a BoltGroup child
  prepends those plate IDs to `bolt.parts` so holes pierce the plate as well as the column. A
  pure lexicographic roleKey sort puts `bolt_group_0` before `plate_0`, so the bolt group
  materializes with NO owned plates yet and holes pierce only the main part ("2 delik" instead
  of "4 delik"). The child sorter must add a kind-priority EDGE for every pair (Plate→BoltGroup,
  BoltGroup→Cut, …) INTO the Kahn topological sort, so kind order is a real constraint — a
  reverse dependency (a plate declared to depend on a bolt group) then forms a cycle and is
  REJECTED, which is correct. A kind priority used only as a tie-break among ready nodes is not
  enough: a reverse edge still slips through.
- **No silent `ok=true` in the materializer.** `applyCutsFor` returns bool and fails on a missing
  target/base or a null Boolean result; `commit`/`recompute` roll back on any false. A void
  `applyCutsFor` that silently returns on failure is the bug. Cut targets are ROLEKEYS, not
  EntityIds: resolve `main`/`secondary_N` (user parts) or a component-child roleKey to an
  EntityId at commit time; an unresolvable target fails the commit rather than adding a cut with
  a dangling target. Resolving the target is NOT enough — the cut must also be APPLIED to it:
  `commit`/`recompute` call `applyCutsFor` on owned plates and bolt-group parts, so a cut whose
  target is a user part OUTSIDE any bolt group (a fitting on a beam) gets its modifier added but
  its geometry never cut. Track every resolved cut target and `applyCutsFor` each one explicitly,
  or the cut is a no-op that still reports success.
- **Base-version changes must be recorded, and recompute must delete stale children.**
  Wrap `setBaseVersion` so it reads the old base, sets the new one, and calls
  `recordBaseVersionDelta` inside the same undo record — otherwise undo after a recompute snaps
  the pre-cut base back to the old solid. Recompute must DELETE component-owned children no
  longer present in the new plan while never touching the `main`/`secondary_N` user parts.
  Three traps: (a) deleting a stale plate via `deleteModels` triggers the BOLT cascade, which
  can delete a bolt group that is STILL in the new plan — use a cascade-free leaf erase
  (`removeModelLeaf`, a public wrapper over `eraseModelLeaf`) and verify the EntityId is actually
  in `ownedPlateIds`/`ownedBoltGroupIds` before deleting anything. (b) Cut children carry no
  EntityId (Document stores them index-based), so stale-child cleanup by EntityId cannot find
  them — tag component-owned cuts with `owner = componentId` at commit/recompute and remove
  `owner == componentId` cuts before re-adding, or old/changed cuts accumulate on every recompute.
  (c) geometric relationships: after `ConstraintSystem::solve()`, the solver's Conflict/Unresolved
  state is not enough — the PREPARED child position can violate the constraint while the solver
  reports Solved. Verify the produced geometry satisfies n·p = rhs within ±1e-3 (use the plate
  center / bolt origin / cut plane origin), else return Conflict. A `Coincident` that the
  generator ignored must not materialize as success.

- **BoltGroup's local frame v = axis×xDir, but ProfileSectionFrame's height = axis×width — OPPOSITE sign.** `boltFrame()` builds `v = normalize(axis) × normalize(xDir)` (right-handed), while `profileSectionFrame` builds `height = axis × width`. So a generator that sets `bolt.axis = sec.width, bolt.xDir = lenDir` gets `v = sec.width×lenDir = -sec.height` and the bolts fan out in the WRONG vertical direction (down instead of up, j=0 becomes the TOP row). To make bolts follow +sec.height, NEGATE the axis (`bolt.axis = {-sec.width}`) and keep `xDir = lenDir` so columns still run along +lenDir and the head/nut side is the only thing that flips. A centered single-row layout hides the flip (the SET of positions is symmetric) but a multi-row or multi-column layout does not.
- **Bolt grip (display length) must come from drilled thickness, not the member bbox — and grip must NOT be `holeDepth`.** `Document::boltGripLength` measures the min-max span of `bolt.parts` bboxes along the axis; a beam in `parts` contributes its flange WIDTH (~300 mm for HEA300), so the bolt renders absurdly long ("civata boyları uzun"). Carry `secWebThickness`/`secFlangeThickness` on `MemberGeometry` (fill from the profile catalog's `plateThickness`/`flangeThickness` — NOT from `secWidth`/`secHeight`, which are section bbox dims) and let the generator compute grip = plate + drilled thickness. Keep it in a display-only `gripLength` field on BoltGroup, SEPARATE from `holeDepth`: `holeDepth` feeds the hole cut and, when >0, `occ_geometry` centers the finite cylinder at `planeOrigin` (the grip centre), so setting `holeDepth` without also shifting the bolt origin to the grip centre cuts holes in the wrong place (disjoint/partial). `gripLength` only feeds `buildBoltGroupAssemblyShape`; it must never reach the hole modifier. Gate it on `secWebThickness > 0` so a profile-less member keeps the legacy bbox fallback. The grip LENGTH is only half the fix — the bolt must also be CENTERED on the drilled stack, not on the web centreline. `buildBoltGroupAssemblyShape` centres the grip on `bolt.origin`; a generator that leaves origin at the web centreline (tabBase) while the plate sits `ecc` away renders the bolt shifted by half the stack, so head/nut float off the faces ("civata başlangıç bitiş yeri kaymış, uzunluğu doğru"). Shift origin along the axis to the stack centre `(ecc + t/2 − w/2)/2` (sign + toward the near tab, − toward the far tab, 0 for a double tab) and set grip = `ecc + t/2 + w/2`. That axis-direction shift is SAFE for holes: through-cuts (`holeDepth <= 0`) span the whole bbox, so hole positions in the plate do not move.

- **Component materializer sets NO display colour — plates and bolts render default white unless you paint them.** `ComponentMaterializer::commit`/`recompute` build the plate and bolt `WireframeModel`s with `shapeToWireframeWithFaces` and never assign `trueColor`/`effectiveColor`. Tekla-style convention: bolts brown `0x8B592A`, plates **`kPlateGreyRgb` = RGB(170,170,170) = 0xAAAAAA** — levha grisi ARTIK CONTRACT SABITIDIR (`libs/mm_contract/include/mm_contract/component_visual.hpp`), 0x808080 kullanma; Studio ayni sabite baglanir. Levhalar rol/renk ayrimi yapmadan HER YERDE gri (model, komponent onizlemesi, canli mock view); secim/hover vurgusu rengin USTUNE cizilir, profiller ve bulonlar kendi renklerini korur. Apply it in BOTH the commit path AND the recompute path (recompute rebuilds the display from scratch) via one `applyColor(WireframeModel&, uint32_t)` helper, not four inline copies. The manual bolt command and End Plate already paint bolts brown; the materializer must match or the same bolt renders white when it comes from a component. **"Levha yeşil görünüyor" şikayetinde önce seçimi kaldırt:** seçim (özellikle assembly seçimi) komponentin owned levhalarını da kapsar ve seçim vurgusu levha grisinin ÜSTÜNE çizilir; boş alana tıklayınca yeşil kayboluyorsa veri değil vurgudur, kod değiştirme. Kalıcı yeşil = kaynak katı/glif rengi. Veri yolunu iddia etmek yerine kanıtla: modeli runRecipe ile kurup `ownedPlateIds` renklerini okuyan test (hepsi `0xAAAAAA`) — "depoda doğru renk var" ile "ekranda yeşil" aynı şey değildir.
- **Component-internal CUTS must set `CutModifier::hidden = true`, or they render as floating orange cut planes in the connection.** `renderer.cpp` `drawCutPlanes` skips only `!enabled || hidden`, then draws every PlaneCut/Fitting kind as a visible semi-transparent plane; `CutKind::Hole` is filtered by the kind check, so holes need no flag. The End Plate path already sets `fit.hidden = true` ("component ic motor elemani — cizilmez/secilmez"); a generator that emits a beam end cut (`CutKind::PlaneCut`) without the flag leaks a plane the user reads as "gizli objectler gözüküyor bağlantıda". The flag survives `ComponentMaterializer::commit` (it copies `pc.cut` and touches only `enabled`/`owner`), so set it in the GENERATOR and assert `c.hidden` in the OCC-off generation test.
- **Verify drilled-hole placement by analytical VOLUME, not by counting modifiers.** A plate W×H×T with N holes of radius r has volume `W·H·T − N·π·r²·T`. If the measured OCC volume is off by exactly k hole-volumes, k holes landed OUTSIDE the plate (the bbox-disjoint guard skipped their boolean) even though the cut-modifier count is correct — a "12 delik kayitli" assertion passes while the geometry is wrong. The same trap bites tests that assert only `origin.z`: it passes both signs of a frame flip. Assert the full bolt-origin set (or the drilled-solid volume) in the OCC test, not the modifier count or a single origin coordinate.
- **A code path gated on a newly-added field is DEAD in every test whose fixture leaves that field at its default — the fix reads 'verified' while no test exercises it.** The 146 end-to-end `makeSetup()` never set `secWebThickness`, so the `if (secWebThickness > 0)` grip/centering branch ran only in the OCC-off generation-plan test (no OCCT materialization) and the real bolt-hugging geometry was never asserted until a case set the field. When a fix is gated on a new field, add a test case that SETS the gate field and asserts the gated GEOMETRY (bolt origin at the stack centre), not just the plan-level `gripLength`.
- **External-file → native component: bind by PROFILE, never by axis LENGTH or selection order.**
  When a connection is reconstructed onto the user's existing members, compatibility is the member's
  SECTION/TYPE, not its length: the same physical column is routinely drawn at a different length in
  another model (Tekla 7200 mm vs 3382 mm here), so a length comparison rejects the correct member
  and the user sees "uyumsuz: eksen boyu". Equally, binding the FIRST selected member to `primary`
  breaks as soon as the user clicks the beam first. Collect the selection into one pool, match each
  target by `profileName` (case-insensitive), and only fall back to an explicit GUID map — never
  auto-pick an unmatched member; report the profile reason instead.
- **When a placement fix passes the unit test but is still wrong in the user's model, the fixture does NOT match the model — instrument before the next guess.** Iterating on unit-test-verified hypotheses for a geometry bug, each green on the fixture yet still wrong on the user's machine, burns turns and patience. Generators are pure, so add an env-gated diagnostic dump (`if (std::getenv("MM_<COMPONENT>_DIAG"))` → `std::ofstream("<component>-diag.log", std::ios::app)`) recording the ACTUAL member axes (from/to), refNormal, section dims, computed face point, and plate/bolt positions; ask the user for that log and read the real numbers before the NEXT fix. One blind fix after the fixture diverges is already one too many.
 - **Dönük kesit (±90°) + SectionAnchor Top kesiti YANA kaydırır — hata değil, ölçüyü buna göre yap.**
  Üyenin `refNormal`'i kendi eksenine paralel olduğunda (kolonlar sık sık `sectionNormal=(0,0,1)` ile
  çizilir) `profileSectionFrame` dünya X/Y'ye düşer; 90° dönüşte kesitin `height` yönü kirişin geliş
  yönüne düşer, dolayısıyla Top ankrajı ofseti kesiti düşeyde değil YATAYDA h/2 kaydırır ve üye ekseni
  kendi kesitinin bir YÜZÜNDE kalır. Plan ile materyalize katı AYNI sonucu verir; çerçeveyi "ekseni
  kesit merkezine al" diye düzeltmeye kalkma — kullanıcının kolonlarını kaydırırsın. Komşu yüzünü
  destek fonksiyonundan hesapla (`|dot(lenDir,colSec.width)|·w/2 + |dot(lenDir,colSec.height)|·h/2 +
  offset·aH`) ve yüzü eksende değil, eksenden h/2 uzakta bekle.
- **Generator desteklemediği girdiyi AÇIK limit ile reddeder; fixture'ı limitten habersiz kurma.**
  Haunch 40, `|dot(lenDir, Z)| > 0.65` (≈40°'den dik) kirişi "duseye yakin kiris desteklemiyor"
  diyerek reddeder. Böyle bir probe `ok=false` döndüğünde bu bir yerleşim hatası gibi okunur ve
  saatler kaybeder; probe'yu gerçek çatı eğimiyle (~10°) kur, sınırı aşan durumu ayrı test et.
- **`.mmw` içindeki türetilmiş (B-Rep/plaka) geometri açılışta YENİDEN ÜRETİLMEZ.** Yerleşim
  düzeltmesinden önce kaydedilmiş bir dosya, yeni build'de de eski plaka/kesim konumlarını gösterir —
  kullanıcı "hâlâ yanlış" der. Teşhis: kayıtlı model bbox'ı (`WireframeModel::vertices()`) ile
  jeneratörün ürettiği planı (levha merkezi + kesim düzlemi) yan yana yaz; sabit fark = dosya eski,
  çözüm bileşeni yeniden uygulamak (Modify/Apply), kod değiştirmek değil.
- **Fixture geometry must be ANCHOR-CONSISTENT, or contact-dependent features fail while everything else passes.**
 `extrudeProfileSolid` defaults to `SectionAnchor::Top` (section spans -h..0) while
 `MemberGeometry.sectionCenterOffset` defaults to 0 (centroid assumption). The gap is exactly h/2 —
 150 mm on a 300 mm section — so a shear tab's contact point lands OFF the solid, the weld reports
 "temas yok", and the user sees no weld; the beam CUT and the BOLTS still look right because they are
 positioned from the beam, not from the column face. That asymmetry is what makes the fixture look
 healthy. Fix the fixture, never the assertion: extrude with the anchor the MemberGeometry declares
 (`SectionAnchor::Center` + offset 0, or `Top` + -h/2, `Bottom` + +h/2), and only THEN assert
 `geometrySupported`, seam length > 0, a display EntityId and a positive-volume OCCT solid. Deleting
 the geometry assertion to turn the suite green is how this bug survives for sessions.

- **Tam derleme ve 158-testlik ctest tek foreground çağrısına sığmaz: `background=true` ile başlat, `process(action="wait")` ile bekle.** Ana build (~150 hedef) ve ctest (bazı drawing GUI testleri tek başına ~360 sn) foreground zaman aşımında (≈420 sn) kesilir ve `ninja: build stopped: interrupted by user` bırakır; bu "derleme bozuk" demek DEĞİLDİR — aynı komutu background'da yeniden çalıştır, ninja kaldığı yerden devam eder. ctest bitmeden ikinci bir ağır build başlatma (3.7 GB host): sırayla yürüt; ilerlemeyi `tail /tmp/<log>` + `pgrep -f ctest` ile izle.
- **"Kopya/taşıma delta yönü ters" şikayetinde ÖNCE zinciri doğrula, sonra `workPlane_`'i sorgula.** Bu kodda delta zinciri kanıtlanmış doğru: panel → `applyLinearDelta` → `workPlaneDeltaToWorld(workPlane_,delta) = u·dx + v·dy + n·dz` → `linearCopyOffsets = k·delta` → `copyModels(translate(+displacement))`; `tests/workplane_delta_test.cpp` bunu doğrular ve `workPlaneAxisGlyph` de aynı `u/v/normal`'i çizer. İşaret ters görünüyorsa asıl şüpheli `workPlane_` kurulumudur: `WorkPlane::fromCurrentView`'da `u = viewUp × viewDir` olup üstten bakan görünüşte `u = -X` verir (UCS X oku ekranda SOLA bakar) — "geçerli görünüş" UCS'i kullanan kullanıcı bunu ters algılar. Teşhis: kullanıcıya (a) görünüş/UCS türü, (b) yazılan değer, (c) kopyanın OKUN gösterdiği yöne mi TERSİNE mi gittiğini sor; ok ile kopya aynı yöndeyse kod değil `workPlane_` kurulumu sorunludur.
- **Yüz aracı (work plane tool) UCS'yi GERÇEKTEN set eder; "world yönünde gidiyor" hissi `Ucs::fromPlaneAndNormal`'ın u/v seçim kuralından gelir, hatadan değil.** fromPlaneAndNormal `u = cross(ref, normal)` kurar (normal dünya-Z'ye yakınsa ref=Y, değilse ref=Z). Eksen-hizalı yüzlerde bu dünya-permütasyonu üretir: yatay yüz (kiriş üstü) → u=+X,v=+Y,N=+Z (birebir world); +X'e bakan düşey yüz (kolon yanı) → u=+Y(YUKARI),v=+Z,N=+X ("X=100" kopyayı YUKARI taşır); +Y'ye bakan düşey yüz → u=−X(SOL). Kopya okları TAKİP eder, UCS set edilmiştir; kullanıcı yalnız okların YÖNÜNÜ beklemiyor (Tekla u eksenini genelde kiriş eksenine/yataya oturtur). Tıklama işleyicisi `workPlane_ = Ucs::fromPlaneAndNormal(nokta,normal).toWorkPlane()` yapar ve `cancelWorkPlaneCommand` workPlane_'i SIFIRLAMAZ. Teşhis: yüz aracına tıkladıktan sonra kalıcı UCS oklarının (yeşil hayalet değil) yüze mi world'e mi hizalandığını sor — yüze hizalanıyorsa sorun yön kuralıdır, world'de kalıyorsa gerçek yayılım hatası vardır. ÇÖZÜM (uygulandı): `Ucs::fromFaceAndMemberAxis(origin, normal, memberAxis)` — X = üye ekseninin yüz düzlemine izdüşümü (kiriş boyunca), Y = Z×X, Z = normal; uç yüzünde (eksen normale paralel) X yataya düşer. `memberAxis` = EntityProperties axisTo − axisFrom. Yüz aracı TIKLAMASI ve HOVER ÖNİZLEMESİ aynı fonksiyonu kullanmalı (tutarsızsa ok ile delta ayrışır). Tekla karşılığı "work plane using part face". Regresyon: `tests/ucs_test.cpp` 6b bloğu (web yüzü X=kiriş ekseni, uç yüzü X yatay, eğik kiriş üst yüzü X=yatay izdüşüm).

 ## Fin plate (shear tab) geometry — the correct target

The spec is exact: the plate starts PERPENDICULAR to the column face and is offset from the column CENTER by t/2 (t = the column's section dimension in the beam's approach direction). In `generateFinPlate`:
- The shear tab is welded to the column face and bolted to the BEAM WEB, so its plane is PARALLEL to the beam web: thickness = section width (`sec.width`), length = beam axis (`lenDir`), height = section height (vertical).
- The plate's near edge sits ON the column face, not the column centerline: `halfExtent = |dot(lenDir, colSec.width)|*col.secWidth/2 + |dot(lenDir, colSec.height)|*col.secHeight/2` (rectangle support function), then intersect the beam axis with that plane.
- Bolts drill the BEAM WEB (`b.parts = {beam.id}`), never the column — the column connection is the WELD. Put the bolt line at the plate-length MIDDLE, not on the weld edge.
- Resolve roles by geometry, not selection order: the more vertical member is the column, the more horizontal is the beam.
- `beam.refNormal` comes from the drawing work plane and can be HORIZONTAL, tilting the plate — a shear tab is always vertical, so build the beam section frame against world-up (`{0,0,1}`), falling back to `beam.refNormal` only when the beam itself is vertical.

## Selection / glyph layer — a component is NOT done at materialization

Multi-selection is **Ctrl+click** (`Application::toggleModelSelection`, hit-tested at the cursor);
a plain click replaces the selection (`selectModel` → `assign(1, index)`). Any command that needs
TWO user parts (bind a connection to a column + beam, measure, mirror about two picks) must say
"Ctrl+tık" explicitly — telling the user to "select the column and the beam" is read as two plain
clicks, which keeps only the last one and the command reports a missing member.

Materialized geometry plus a correct `ComponentInstanceRecord` still leaves a component INVISIBLE
when the render/hit-test layer only knows the legacy store. Treat the selection surface as part of
the component's definition, not as GUI polish.

- **Render one list from ALL stores.** Render and hit-test loops historically iterate one concrete
  store (`document_.endPlateConnections()`), so a type created through the registry/instance store
  gets no glyph and cannot be clicked while the old type keeps working — it reads as "the symbol is
  missing", not "the loop is incomplete". Add ONE portable core collector (e.g.
  `component_glyph.hpp/.cpp`: anchor = owned plate bbox center, `entityId`, `componentId`,
  `universal`; fall back to the bolt-display center when there is no plate) that walks every store,
  DEDUPES by backing EntityId (legacy connection wins) and skips `enabled=false` / `orphan` /
  invalid ids. Consume that same list in the render build AND in `componentGlyphModelAt` (nearest
  within the pixel radius) — two loops that each iterate one store drift apart immediately.
- **Selection identity = the parent record, resolved through ownership.** The glyph/child click
  selects the owned plate model; the parent is then resolved by matching the selected model against
  `ownedPlateIds`/`ownedBoltGroupIds` (`selectedComponentInstance`). Do NOT introduce a parallel
  "selected component" state — that would fork End Plate's behaviour instead of matching it. Glyph
  is drawn screen-scaled as a 3D SPHERE (filled circle + top-left highlight + dark rim) coloured by
  component TYPE, and tested BEFORE the normal model hit-test because thin plates are unclickable.
  Type flows as `ComponentGlyph.typeId` (End Plate → `L"beam_to_column_end_plate"`, universal →
  `rec.typeId`) → `DraftView.componentGlyphTypes` → `componentTypeColor()` (component_glyph.hpp/.cpp):
  fixed palette for the 4 built-ins (end plate blue, fin plate green, shear-146 orange, haunch purple),
  FNV-1a hash → hue for unknown/custom types, turquoise when empty. `componentTypeColor()` returns
  0xRRGGBB — the GDI renderer must convert to COLORREF (0x00BBGGRR) via
  `RGB((c>>16)&0xFF, (c>>8)&0xFF, c&0xFF)`. Selected glyphs render yellow.
- **Edit UI must be generated from the param schema — never a per-type dialog.** Write ONE
  `openComponentParamsDialog(const ComponentInstanceRecord&)` that renders `ComponentType::params`
  (`CompParamSpec`: Length/Angle → QDoubleSpinBox with unit suffix and min/max, Integer → QSpinBox,
  Boolean → QCheckBox, Enum → QComboBox, String/Profile/Material → QLineEdit, EntityRef read-only)
  and on OK calls `setSelectedComponentParams(record.id, params)` →
  `ComponentMaterializer::recompute`. Adding a component type then needs zero Qt changes.
- **Wire double-click to an id, not to the selection.** Add a `setComponentEditRequest` callback and
  trigger it from the double-click branch by testing whether the hit model is owned by a record;
  carry the `ComponentInstanceRecord` (hence its id) into the queued dialog — the dialog is opened
  asynchronously, so `selectedModels_` is already cleared by the time OK is clicked.
- **PROPS panel**: when the selected child belongs to a component, append type name, typeId,
  component id and every aligned parameter (`alignParamsToSpecs` + `findParam`), so the connection
  reads as one object instead of a loose plate.
- **Test the layer, not just the geometry.** The collector is core-only, so a
  `tests/<topic>_test.cpp` in the OCC-off ctest locks the dedupe/skip rules; the OCC end-to-end test
  then asserts glyph presence and identity across commit → undo → redo → MMW8 reopen → recompute
  (the anchor must MOVE with the recomputed geometry, and the parent id must survive).

## Schematic dialog layer (component_dialog)

The Tekla dialog has a PICTURE pane (schematic + numbered hotspots) beside the param list.
`component_dialog.hpp/.cpp` is the portable (OCC-free, windows.h-free) presentation layer for it:

- `DiagramPrimitive`/`ComponentDiagramItem` — normalized 2D schematic (Rect/Line/Circle/Text/Polyline),
  screen/DCI-independent; `DiagramHotspot` binds a pick point to a param name.
- `ComponentDialogSchema`/`ComponentDialogGroup` — items + hotspots + ordered param groups;
  `validateDialogSchema` (unique ids, valid param bindings) + `hitTestDiagramHotspot`.
- `EndPlateParams`<->`CompParam` adapter: `endPlateDialogSpecs` (27-field superset of the engine
  schema), `endPlateToParams`/`endPlateFromParams` (safe defaults on invalid enum).
- `ComponentDialogRegistry` + `endPlateDialogDefinition`/`finPlateDialogDefinition` +
  `registerBuiltInDialogs`.

A new connection type registers a `ComponentDialogDefinition` (specs + schema) ALONGSIDE its
`ComponentType`; the param-list pane is generated from specs, the picture pane from the schema —
one dialog surface, no per-type Qt widget. Test: `tests/component_dialog_test.cpp` (OCC-off ctest).

Pitfalls:
- `ComponentDialogSchema.typeId` is a SEPARATE field from `ComponentDialogDefinition.typeId`.
  Set BOTH when building a definition; otherwise `validateDialogSchema` fails with "typeId bos".
- `CompParamSpec` and `CompParam` have NO `operator==`. Comparing a whole
  `std::vector<CompParamSpec>` with `==` does not compile — compare field-by-field or by `name`.

### 3B önizleme HER tip için varsayılan — `typeId` kapısı yasak

- Diyaloğun 3B canlı önizleme + ön/yan/üst ortografik blokları **hiçbir tipe koşullanmaz**.
  Bir zamanlar `def_.typeId == L"tekla_haunch_40"` ile kuruluyordu; paketten yüklenen her tip
  (or. `mm_end_plate_pkg`) else dalına düşüp kullanıcının "eski 2B şematik form" dediği görünüme
  kalıyordu. Kural: blok her zaman kurulur, 2B şematik `schema.items` varsa "Sematik" düğmesiyle
  açılır (layout'ta kalır, **varsayılan gizli**). Paket tipi ile yerleşik tip arasında UI farkı yoktur.
- `qt_main_window.cpp` içindeki `componentPreviewInput` çağrısını da tipe bağlama: açılış, `Get`
  ve çift-tık yollarının ÜÇÜ de girdiyi her tipe verir; biri atlanırsa 3B pencere açılır ama model
  çizilmez (boş önizleme, hata mesajı yok).
- **Diyalogun İKİ dalı vardır; önizlemeyi yalnız birine koymak yarısını boş bırakır.**
  (a) `ComponentDialogSchema` (sekmeli ya da groups) → `ComponentDialogWidget`; (b) sekmesiz/gruplu
  eski `QDialog` düz form dalı. Paket diyaloğu `useTabs=false` ile gelirse (b) dalına düşer ve
  orada hiç 3B önizleme yoktur — kullanıcı "bu tipte önizleme yok" der. (b) dalına da
  `ComponentLivePreviewWidget` (240 px, üstte) ekle: `setTypeId` + `setPreviewDefinition` +
  girdi + `setParams(alignParamsToSpecs(type->params, rec.params))`.
- **Önizleme girdisi çözümleme sırası:** modeldeki gerçek instance (`componentPreviewInput`) →
  `ComponentDialogDefinition.previewInput` (paketin temsilî girdisi) → ikisi de yoksa mesaj.
  Widget, gerçek girdi verilip verilmediğini bir bayrakla (`previewInputGiven_`) tutar ve fallback'i
  YALNIZ o verilmediğinde uygular; yoksa instance'ın parametreleri temsilî girdiyle ezilir.
  Mesaj her iki dalda da aynı olmalı ve Türkçe karakterli: "Modelde bağlı kolon ve kiriş bulunamadı".
- **`componentPreviewInput` SESSİZCE `nullopt` DÖNMEMELİ.** Eski kod (i) profili katalogda
  bulamayınca ve (ii) ikincil üye yoksa vazgeçiyordu; sonuç: sağlam bir instance'ta bile önizleme
  boş + "bağlı kolon/kiriş bulunamadı" mesajı (kullanıcı bunu yerleşim hatası sanar). Kural:
  profil yoksa modelde kayıtlı `secWidth/secHeight` ile MemberGeometry kur; ikincil üye ZORUNLU
  değildir (taban levhası tek parçadır) — eksikse generatorun kendi hatası gösterilir.
- **Düğme kırpılması = başlıkla aynı satır.** Başlık `stretch=1` ile dururken sabit genişlikli
  görünüm düğmeleriyle aynı satırı paylaşırsa dar pencerede İLK düğmeler ("3D", "Ön") kırpılır.
  Başlığa kendi satırı + `QSizePolicy::Ignored` (elide), düğmelere `setMinimumWidth` ver.
- Otomatik ölçü (MM-6b) yalnız tip için **hiç tanım yoksa** devreye girer (dims VEYA toggles boş).
  Elle yazılmış tanım varsa dokunma; duyarlılık analizinin ürettiği toggle'lar Haunch 40'ın
  berkitme ghost'larını ezer ve mevcut önizleme testlerini kırar (bir turda 4 kontrol).
- Sıra: `setPreviewDefinition` boş `dimensions` ile çağrıldığında mevcut callout'lar korunur —
  "tanım yok" ile "tanım boş" aynı şey değildir; ikisini tek `hasPreviewDefinition` bayrağına
  indirgersen yerleşik callout'ları silersin.

### Sentinel değerler kullanıcıya GÖRÜNMEZ (parametre editörü)

- `-1` ("otomatik") veya `<= -1e8` ("tanımsız") varsayılanlı her Length/Angle/Integer parametre
  "Default" kutusuyla gelir; kutu **işaretsiz**, editör **disabled**, etiket `<ad> — otomatik`,
  editörde nötr değer (0). Sentinel algısı `optional` şartına bağlanmaz (`hasSentinelDefault`).
  İşaretsiz parametre `resultParams()`'a yazılmaz; işaretlenince alan açılır.
- **Editörler kurulduktan SONRA `setParams(current_)` çağrılmalı** (widget constructor'ının sonunda).
  Çağrılmazsa şema varsayılanı ham basılır: kullanıcı "kenar mesafesi -1000000000" görür ve bu
  senin sentinel mantığının değil, kurulum sırasının hatasıdır.

### Offscreen doğrulama (Linux'ta görünmeyen GUI hataları için zorunlu)

`tests/component_dialog_widget_test.cpp` içine paket yüklü bir blok ekle:
`loadComponentPackage(MM_PACKAGES_DIR/"end_plate_pkg")` → `ComponentDialogWidget w(pkg.dialog, {})`
→ `resize(1100,720)`, `show()`, `processEvents()`; sonra iddia et:

- `findChildren<ComponentLivePreviewWidget*>()` boş DEĞİL; `isCompact()` sayısı == 3 (thumbnail).
- 2B şematik görünümü `isHidden()`.
- Tüm `QLabel`/`QLineEdit`/`QAbstractSpinBox` metinlerinde `"1000000000"` YOK; sentinel parametre
  editörü `!isEnabled()` + etikette "otomatik" + değer 0.
- **Paket tipini test ederken paketi TEK BAŞINA yükle:** `loadComponentPackage(MM_PACKAGES_DIR /
  "<paket>", ComponentRegistry::instance())` + `registerType` + `registerDialog`. TÜM paketleri
  yükleyen tarama yerleşik typeId'lerle çakışır (haunch40 paketi = yerleşik `tekla_haunch_40`) ve
  `scan.errors` dolu gelir → test ilk kontrolde düşer.
- **Boş önizlemeyi yakalayan iddia PLAN üzerinden kurulur:** `previewPlan().ok`, `plates`,
  `boltGroups` (≥1 levha + ≥1 bulon grubu). "Widget var mı" kontrolü boş önizlemeyi de geçirir —
  kullanıcının gördüğü tam olarak buydu. İKİ açılış yolunu ayrı doğrula: (a) temsilî girdi
  (parça seçilmeden, `previewInput`), (b) `setPreviewInput(<instance>)`.
- **Diyaloğa AYNI türde İKİNCİ bir widget eklerken (ör. ikinci QTableWidget) her ikisine objectName ver; test `findChild<T*>()`'i İSİMLE arasın.** `findChild<T*>()` ağaçta İLK eşleşeni döndürür — constructor'da weld tablosundan ÖNCE kurulan yeni checks tablosu `findChild<QTableWidget*>()` tarafından seçilir, test onun `->item(r,c)->text()`'ini çağırır ve null item'da SEGFAULT verir. `checksTable_->setObjectName("checksTable")` + `weldTable_->setObjectName("weldTable")` koy ve testi `findChild<QTableWidget*>("weldTable")` ile güncelle. Segfault'u tablo kurucusuna/`resizeColumnsToContents()`'a yorma — `gdb -batch -ex bt` backtrace'i `QTableWidgetItem::text()` gösterir; asıl neden findChild'un yanlış widget'ı seçmesidir.
- Testler yalnız FAIL satırı basar; `TUM KONTROLLER GECTI` + exit 0 = yeşil (PASS sayısı yok, boş
  çıktıyı "hiç kontrol koşmadı" diye okuma).
- Geniş bir widget değişikliğinden sonra MEVCUT offscreen test kırmızıysa, değişikliği stash'leyip
  yalnız o hedefi bir kez koşarak taban çizgisini doğrula; kırılma yeniyse sebebi daralt (yukarıdaki
  toggle ezme tuzağı ilk şüpheli).

## Adapting a Tekla dialog whose geometry is only partly implemented

Show the full Tekla tab layout, but fields with no engine geometry go in as
`CompParamSpec.documentedOnly=true` (greyed, tooltip, never written to `resultParams()`, never
added to `ComponentType::params`) — hiding them loses the Tekla look, enabling them lies about
what the model does. Lock it with a test: grey names absent from the engine schema, every
non-grey name present. Count-asserting tests (`specs.size()==N`) must count `!documentedOnly`.
Tab/param mapping comes from the Tekla page, not from the existing grouping. Widget-side
details live in the dialog-editor reference of the `model-maker` skill. Full procedure, the
hidden-tab trap for derived user-catalog templates, and the help-page figure recipe:
`references/tekla-dialog-adaptation.md`.

## Tekla documentation workflow (4 iterations)

1. Parse the Tekla page: parts, parameters, references, defaults. Mark anything not stated in the
   doc as UNVERIFIED — never invent a Tekla default as if it were exact.
2. Compose geometry from existing primitives + relationships.
3. Recompute, ownership, undo/redo, MMW8 serialization, real OCC tests (not 2D-wireframe mocks).
4. GUI integration (dialog tabs), independent review, acceptance.

## JSON codec / digest determinism (MM-2)

- Kanonik JSON: anahtarlar sirali, double'lar `std::round(x*1e6)/1e6` + `%.6f`, `-0 -> 0`, UTF-8. Bu kombinasyon Linux GCC ile mingw-w64 arasinda BIREBIR ayni SHA-256 digest verir (kanitlandi).
- `M_PI` mingw'de tanimli DEGILDIR (POSIX'e ozel) — test/deterministic fixture'da kendi `constexpr double kPi = 3.14159265358979323846;` sabitini kullan. Linux'ta derlenip mingw'de patlayan tipik tuzak.
- Cross-derlenmis Windows exe'sini Wine ile kosup digest'i Linux exe ile `diff` etmek GERCEK determinizm kanitidir (tahmin degil). `libgcc_s_seh-1.dll`/`libstdc++-6.dll` yoksa exe acilmaz (status c0000135) → `-DCMAKE_EXE_LINKER_FLAGS="-static -static-libgcc -static-libstdc++"` ile linkle.
- `wstring <-> UTF-8` portable donusum: `sizeof(wchar_t)` ile UTF-16/32 ayrimi + surrogate handling; contract katmaninda Win32 `WideCharToMultiByte` KULLANILMAZ (windows.h yasak).
- `formatVersion` her kok objeye yazilir; nested encode'da `...Impl` (formatVersion'siz) kullan, yoksa her alt nesne tekrarlar.

## Recipe yorumlayici + digest esdegerligi (MM-3)

- Uyum YALNIZ digest esitligiyle olculur: `generationPlanDigest(runRecipe(recipe,specs,in)) == generationPlanDigest(generator(in))`. Bir golden test dusunce golden dosyasini YENIDEN URETME; farki raporla.
- Mevcut bir generator'u recipe ile birebir eslestirmenin guvenli yolu, onun YERLESIM fonksiyonunu TEK bir BILESIK tasa sarmaktir (`endplate.placement` -> `computeEndPlatePlacement`; bir `PlateSpec` + bir `BoltGroup` uretir). Geometriyi recipe ifadeleriyle yeniden turetmek her kucuk sapmada digest'i bozar; sarma esitligi KONSTRUKSIYON geregi garantiler. ~400 satirlik bir generator'u (haunch40) granular taslara ayirmak yuksek drift riskidir — once sar, granulerlestirmeyi sonra yap. MEVCUT GENERATOR'A DOKUNMA (kullanici kurali: generateEndPlate/generateHaunch40 vb. degismez); esitligi yalniz sarma ile sagla.
- Saran tas parametreleri generatorun okudugu YOLDAN okumali: `paramAsDouble(input.params, L"plateThickness", 12.0)` (recipe'nin hizalanmis param haritasini DEGIL). Fallback/varsayilan farki digest'i kaydirir.
- Blok kayit defteri numArgs (sayisal ifade argumanlari) ile strArgs/listArgs'i AYIRIR; statik analiz YALNIZ numArgs ifadelerini tarar. Aksi halde "8.8"/"main"/"S235JR" gibi sabit metinler "tanimsiz let" sanilip dogrulama yanlis duser.
- Iki nesne ureten bilesik blokta rolleri ARGS ile ver (`rolePlate`/`roleBolts`); roleKey formati/tekilligi/tur tutarliligi her iki tur icin ayri dogrulanir.
- `if()` iki dali da (eager) degerlendirir — guvenli, cunku statik analiz iki dalin da gecerli oldugunu garanti eder (0'a bolme IEEE inf/nan uretir, atmaz). Trig fonksiyonlari DERECE alir (Angle parametreleriyle tutarli).
- contractVersion bump'inda O TARIH HER testteki sabit surum dizesini guncelle (smoke test VE codec test ayri ayri `kContractVersion` dogrular); birini guncellemek digerini dusurur -> once `grep -rn 'kContractVersion ==' libs/mm_contract/tests`. AYRICA smoke test'te `!contractSupports(1, N)` "ust minor" kontrolu var: surumu N'ye cikarinca o satiri `!contractSupports(1, N+1)` yap VE `contractSupports(1, N)` 'desteklenir (mevcut)' olarak EKLE — yoksa "contract N desteklenmez (ust minor)" duser. recipe_test de ayri bir `kContractVersion ==` satiri tasir; smoke+codec+recipe ucunu birlikte guncelle.
- Iki implementasyon nominal olarak FARKLI girdiler icin AYNI digest'i veriyorsa, mevcut kod o alani HESABA KATMIYORDUR (or. generateEndPlate `profileRotation`'i `computeEndPlatePlacement`'a gecmez). Testi "yanlis" sanma — alanin hesaba ulastigini dogrula, durumu raporla, gizleme.
- Contract standalone build: `cmake -S libs/mm_contract -B build-contract`; testler `libs/mm_contract/tests/` altinda ve O CMakeLists'te kayitli (kok ctest baseline'da kalir). Yeni bir `src/<x>.cpp` eklerken HEM kok `CMakeLists.txt` (mm_component_contract) HEM `libs/mm_contract/CMakeLists.txt` listesine ekle; biri eksikse standalone build/link kirilir. **Ters yon de gecerli:** `src/` altindaki bir contract kaynagi YALNIZ `libs/mm_contract/CMakeLists.txt`'e eklenirse bagimsiz contract testleri yesil kalir ama uygulama hedefi `undefined reference` verir — MM-6b'nin `component_preview_auto.cpp`'si bu yuzden uygulamaya hic girmemisti. Dogrulama: `grep -c <dosya>.cpp build-occ-hermes/build.ninja` (0 = grafik bilmiyor, `cmake -S . -B build-occ-hermes` ile yeniden yapilandir).

## Buyuk generator'u (haunch40) taslara ayirma (MM-3c C1/C2/C3)

~600 satirlik bir generator'u granular taslara ayirmak, "once sar" kuralinin ISTISNASIDIR: kullanici acikca "generator'a dokunma kuralini bu adim icin kaldiriyorum, ama golden digest HIC degismesin" derse izlenen guvenli yol:
- **C1 — ic refactor, digest sabit.** Govdeyi adlandirilmis fonksiyonlara parcacik parcacik tasi (contact frame, plumb face/levha boyutu, web konturu, flans, kapak+berkitme, bulon gruplari). Aritmetigi BIREBIR kopyala; `L - (L-25.0)` gibi ifadeleri `25.0`'a SADELESTIRME (float yuvarlama digest'i kaydirir). Dogrulama: ayni 5 golden digest. C1 commit'ini `git add` ile yalniz dokunulan .cpp'ye sinirla.
- **C2 — taslari disa ac, parametre args'tan.** Opak durumu (`std::shared_ptr<void>`) adimlar arasinda tasi. En az riskli kopru: `runHaunch40Block(name, HaunchParams, input, state&, plan)` fonksiyonu HaunchParams'i GECICI bir `ComponentInput` kopyasinin `params`ina enjekte eder ve C1 fonksiyonlarini AYNEN cagirir. Boylece generator fonksiyonlari degismez (digest sabit), parametreler recipe args'tan gelir (gizli input.params okumasi yok) ve ayni taslar baska komponentte kullanilabilir.
- **Ayni isimli tip carpismasi (anon namespace vs public baslik).** Bir tipi HEM public baslikta (forward-declare veya tam tanim) HEM .cpp'nin `namespace { ... }`'inde ayni adla tutma: `mm::X` ve `mm::{anon}::X` iki AYRI tip olur; `x = y` -> 'no match for operator=', fonksiyon imzalarinda 'was not declared in this scope' hatalari cikar. Kural: (a) opak ortak durum paylasacaksan baslikta forward-declare ETME, API'yi `std::shared_ptr<void>` ile tut, .cpp icinde `static_pointer_cast<X>` yap; (b) .cpp'de basliktakiyle ayni adli bir struct'i (alanlar ayni olsa bile) YENIDEN TANIMLAMA — yerel kopyayi sil, basliktakini kullan (yoksa `g.upper = prm.upper` tip uyusmazligindan derlenmez). Yalniz yerel bir yardimci tip gercekten farkliysa ona AYRI ad ver (`H40BoltGroup`).
- **C3 — paket + esdegerlik testi.** `packages/haunch40/recipe.json` + `tests/haunch40_equivalence_test.cpp`: 5 golden girdide `digest(runRecipe) == digest(generateHaunch40) == golden dosyasi`. Golden JSON'u harici kutuphane olmadan basit substring aramayla oku (`"key"` -> `:` -> tirnak icindeki 64 hex); `MM_CONTRACT_GOLDEN_DIR` tanimini test hedefine ekle.
- **Linux + mingw determinizmi:** cross-derlenen `mm_contract_haunch40_equiv_test.exe`'yi Wine'da kos; `golden dosyasi okundu` kontrolu Wine'da Linux yolundan dolayi DUSER (beklenen) — yalniz `gen=/rec=` digest satirlarini `sed -E 's/.*gen=([a-f0-9]{64}) rec=([a-f0-9]{64}).*/\1 \2/'` ile cikarip Linux ciktisiyla `diff` et; bos diff = identik.
- **comment/`kContractVersion` bump her testte ayri.** MM-3c'de 1.3.0'a cikinca smoke/codec/recipe Uc testin sabit dizesini de guncelle; yalniz birini guncellemek diger ikisini dusurur.

## Digest-korumali davranis duzeltmesi + tas bagimsizligi (MM-3d / MM-BUG-01)

- **Golden digest korumali hata duzeltmesi: yalniz ETKILENEN girdide sapmayi uygula.** Bir generator'u (or. `generateEndPlate`) duzeltirken diger girdilerin digest'i degismesin diyorsan yeni fonksiyon, etkisiz/duyarli olmayan durumda (or. `profileRotation == 0`) ESKI fonksiyona BIREBIR `return` etsin; yalniz duzeltilmis durumda yeni yolu isletsin. Boylece 5 golden digest'ten yalniz etkilenen (90°) degisir, kalan 4 bit-bit ayni kalir. Eski kodun float davranisini koru (`L - (L-25.0)`'i `25.0`'a sadeletirme).
- **Duzeltmeyi HEM generator'a HEM recipe tasina uygula.** `digest(runRecipe) == digest(generator)` esdegerlik testi, iki taraf ayni davranisi uygulamazsa TAM OLARAK duzeltilen girdide kirilir. Generator duzeltilip tas (or. `endplate.placement`) eski mantikta kalirsa 90° digest'i esit cikmaz. Ikisi de ayni paylasilan fonksiyonu (or. `computeEndPlatePlacementRotated`) cagirsin.
- **Golden dosyasi her zaman degismez.** Duzeltme davranis degistirse bile golden dosyasi etkilenen girdiyi icermiyorsa guncelleme GEREKMEZ — commit mesajina hangi digest'in neden degistigini (eski->yeni deger) yaz, dosyayi yeniden URETME. "Golden guncellemesi ayri commit olsun" kurali, ancak gercekten bir golden dosya degisiyorsa uygulanir.
- **Cok maddeli davranis turu: GOLDEN BASELINE ONCE (ayri commit), sonra madde basina KOD + AYRI golden commit.** Yeni bir golden dosyasi acarken (or. end_plate_digests.json) baseline'i esdegerlik testini kosup o anki digest'leri yazarak ayri commit'te olustur. Davranis degistiren HER madde = KOD commit'i + AYRI golden-update commit'i (eski->yeni digest eslesmesi commit mesajinda). Davranis DEGISTIRMEYEN madde golden commit'i GEREKTIRMEZ: or. 11 elle alan okumasini `endPlateFromParams` (27 alan tek kaynagi) ile degistirmek, varsayilanlar birebir ayni oldugu icin hicbir digest'i kaydirmaz. Hangi digest'in degistigini TAHMIN ETME — esdegerlik testini kosup ciktiyi gor, yalniz degisenleri yaz.
- **Bir default degeri degistirince test agacini eski default'un LITERAL'i icin grep'le.** Testler eski default'u assert eder (or. "2x4 bulon (default)", "eksik param boltCountY -> varsayilan 4") ve ayri guncellenmedikce ana ctest duser. Default ayni anda TUM okuma yerlerinde degismeli — tip semasi (`endPlateComponentType`) + generator fallback + recipe tasi fallback; biri unutulursa `gen==rec` kirilir. Generator ile tas ayni parametreleri okumaliysa IKISINDE de elle kopyalamak yerine TEK adapter (`endPlateFromParams`) kullan; yeni bir placement pipeline'i (resolveShearTabPlacement + computeEndPlatePlacementOnFace) PAYLASILAN bir fonksiyona cikar (or. `endPlatePlacementFromInput`) ki gen==rec yapisal garanti olsun.
- **Bir degerin NEREDEN turedigi degisince (sadece default degil), duz unit testteki HARDCODED sayilar eskir — golden digest ile OVUSMEZ, ayri yakalanir.** `makeEndPlateBolts` spacingY'yi levha yuksekliginden kesit yuksekligine gecirince (ve boltEdge default'u 30->35 olunca) `end_plate_test.cpp`'nin hardcoded `spacingY 280`, `origin.y -90`, `origin.z -140` degerleri kalir; golden guncelleme AYRI commit'te yapilsa bile golden digest ile duz `end_plate_test` birbirini ORTMEZ, yani golden-commit yesil iken ana ctest kirmizi kalabilir. Duzeltme sirasi: davranis degisince ONCE tam ana ctest'i kos (contract testi veya golden yeterli degil), sonra yeni degerleri ELDEN turetmek yerine placement fonksiyonunu dogrudan cagiran ~20 satirlik bir probe ile bas (`g++ -std=c++20 -I include probe.cpp src/end_plate.cpp src/geometry.cpp src/bolt.cpp` — `computeEndPlatePlacement` cagirip spacingX/spacingY/origin'i yazdir) ve testi TAM O sayilara guncelle; elden 280->240 / -90->-85 / -140->-115 turetmek bir ofset daha hata riski tasir.
- **Tas bagimsizligi (her tas kendi args'i + adi gecen frame).** Monolitik generator'u "gizli ortak durum yok" bicimine bolme adimlari: (1) resolve fonksiyonunu SAF parcalara ayir — geometri PARAMETRESIZ, sizing/plumb ayri; aritmetik birebir. (2) Her tas, recipe'de adi gecen onceki frame'in (step id) KOPYASINI alir, yalniz KENDI degiskenlerini set eder, ilgili C1 fonksiyonunu cagirir, ciktisini yeni step id'ye yazar. Frame'leri EvalCtx'te `std::map<std::string, std::shared_ptr<void>>` ile tasi. (3) `role` arg'i tek-cikti taslarda uretilen roleKey'i recipe'de gorunur kilar; BOS birakilirsa sabit anahtar korunur → golden digest degismez (recipe'de role = generator'un sabit anahtari). Cok-cikti taslarda (berkitme/kaynak) sabit anahtarlar kalir. (4) Kabul testi: tek bir tasi (or. `haunch.web`) YALNIZ bagimli oldugu frame ciktisi + kendi args'iyla calistirip plan uretebilmek — "baska tasin gizli durumunu okumuyor" iddiasinin kaniti budur.

- **Surum hem metin hem sayi ise, ikisini karsilastiran test ekle.** `kContractVersion = "1.3.0"` ile `kContractVersionMinor = 2` ayrisirsa `contractSupports(1,3)` yanlis doner ve kimse fark etmez (metin testi yalniz string'i dogrular). Bir test `std::sscanf(kContractVersion, "%d.%d.%d", ...)` ile ayristirip major/minor/patch sabitleriyle karsilastirsin. `contractSupports(minMajor,minMinor)` semver: major FARKLIYSA false (ne ust ne alt); ayni majorda `minor >= min`. "Kontrat daha yuksek major'da, o halde alt-major istegi de destekler" yanlistir (major degisimi kiricidir) — `kContractVersionMajor > minMajor ||` kolunu EKLEME.

 ## Kanonik uye geometrisi + modelden kurulan girdi (ST-3g / contract 1.8)

 - **Kanoniklestirme refNormal VEKTORUNU degil, kesit CERCEVESINI korumali.**
 `memberGeometryFromProfile` (component_engine.cpp) ayni fiziksel uyeyi uctan/isaretten bagimsiz
 AYNI kesit cercevesine indirgemek icin var. Cerceve `profileSectionFrame(axis, refNormal)` ile
 kurulur; kanoniklestirme refNormal'in isaretini veya keyfi fallback'ini degistirip cerceveyi
 dondururse sectionCenterOffset yonu ve profileRotation tabani da doner. Dusey kolonda
 (sectionNormal={0,0,1} ∥ axis) 90° donukken bu, cap/alin levhasini kolon tepesinden yana kaydirir.
 - **Dejenere 'n ∥ axis' fallback'i profileSectionFrame'in KENDI fallback zincirini birebir yeniden
 uretmeli.** profileSectionFrame zinciri: cross(n,axis)=0 → cross(worldUp,axis)=0 → width={1,0,0}
 (sabit). Kanonik fonksiyon burada kendi 'dunya X' refNormal'ini uydurmamali; cross(refNormal,axis)
 ={1,0,0} olacak sekilde refNormal={0,az,0} vermeli VE bu dal isaret-kanoniklestirmeden MUAF olmali
 (az<0 iken {0,-1,0}→{0,1,0} cevrilirse genislik yine ters doner). Kanit: ham (raw refNormal) ile
 kanonik cikti ayni planDigest vermeli. Regresyon: member_geometry_canonical_test 4b (n∥axis
 cercevesi ham ile ayni; 90° ayni doner).
 - **Golden'lar ComponentInput'u C++'ta elde kurdugunda Document→builder yolu KOR NOKTA kalir.**
 GUI'nin gercek yolu application.cpp memberGeometryFor → memberGeometryFromProfile; elde-kurulan
 golden bundan gecmez, o yoldaki regresyon tum testler yesilken gecer. Modelden kurulan altin testi:
 Document kur → `componentInputFromDocument` (include/model_maker/component_input_builder.hpp) →
 generate* → digest, elde-kurulan golden ile karsilastir. Tek kaynak: application.cpp de AYNI
 tasinabilir fonksiyona delege eder.
 - **generationPlanDigest ID'ye DUYARLI: kaynak WeldSpec.partA = main.id (EntityId).** Document
 id'leri insersiyon sirasina gore atadigindan ayni fiziksel model farkli id'lerle farkli digest
 verir. Modelden-kurulan girdiyi elde-kurulan golden ile karsilastirirken id'leri golden'inkiyle
 esitle (normalize et). "Ayni uye → ayni digest" garantisi ancak id'ler de esitse tutar; Studio kendi
 id'sini atar → adaptor yine farkli digest uretebilir.
 - **Hangi uyenin cerceve degisiminin digest'i kaydirdigini kombinatorik izole et.** Her uyeyi
 raw/kanonik olarak AYRI AYRI degistir (raw/raw, canon/raw, raw/canon, canon/canon) ve digest'leri
 karsilastir; ikisini birden degistirip "fark var" demek kaynagi gostermez. (Haunch kiriş icin
 worldUp kullandigindan kiriş refNormal'i digest'i degistirmedi; yalniz kolon degistirdi.)

 ## Recipe parametre akisi + end plate bulon yigini (kanitlanmis tuzaklar)

- **Recipe taslari parametreleri `param.<ad>` ile okumali; sabit yazim diyalogu olu kilar.**
  Recipe `"450"`, `"S235JR"` gibi literaller tasirsa diyalogdan deger degistirmek geometriyi
  DEGISTIRMEZ (Studio bunu ancak OCCT olcumuyle yakaladi). Sayisal argumanlar `numArg`
  uzerinden `param.*` ifadesini cozer; METIN argumanlar (plateMaterial/grade) ve BOOLEAN
  argumanlar cozmez — `strParamArg` + `boolArg`'in `param.` dali eklenmeli. Girdi
  parametresi yoksa SEMA VARSAYILANI kullan (aksi halde parametresiz golden girdiler patlar).
- **Boolean parametrenin varsayilani `defDouble`'a dusmemeli.** `ctx.params` yalniz
  `paramAsDouble` ile kurulursa `createWelds=true` (defBool) sessizce 0 olur; Boolean
  spec'ler icin `defBool ? 1 : 0` yayinla.
- **Diger kabul testinin kapisi**: recipe'yi testte ELLE kopyalamak, JSON'daki degisikligi
  GORMEMEK demektir (ayni test hem eski sabitleri hem yeni parametreleri dogrular). Test
  paketteki `recipe.json`'u `recipeFromJson` ile YUKLEMELI; golden digest'ler yine sabit kalir.
- **Alin levhasi bulon yigini tek kaynak**: grip = levha + gap + kolon flans(lar)i +
  boltLengthIncrease; holeDepth = tek flansta levha+gap+on flans, cift flansta 0 (through-cut);
  saft origin'i yigin MERKEZINE kayar: `0.5*(gap+flans-boltLengthIncrease)` (yoksa bas
  kolon flansinin icinde kalir, Studio olcumu ~14 mm kayma gosterir). `applyEndPlateBoltStack`
  hem generator hem GUI tarafindan cagrilir — GUI'de ikinci bir kopya mantik birakma.
- **Levha kolon yuzune oturunca kiris de kesilmeli**: kiris ucu = levhanin KIRIS yuzu
  (`endPlateBeamCut`: origin = plateCenter + axis*t/2, normal = axis, keepPositive, hidden).
  Kesme yoksa aciklik levha kalinligi kadar kiris tarafinda kalir. Yeni kesme, mevcut
  "end plate kesme yok" iddiasini tasiyan testleri KIRAR — beklentiyi guncelle.
- **Manifest surum alani TEK: `minContractVersion`.** Iki ad (contractVersion/minContractVersion)
  kafa karisikligi uretir; yukleyici eski adi ACIK mesajla reddetmeli, uretici araclar ve
  paketler tek adi yazmali. Paket manifest'i degisince `checksums.json` YENIDEN uretilmeli
  (aksi halde fixture kendi checksum'iyla reddedilir).

## Paket yukleyici (MM-5) — .mmcpkg'den tip kaydi

Kurallar (uygulandi, `src/component_package.cpp` + `include/model_maker/component_package.hpp`):

- **`ComponentGenerator` artik TYPE-ERASED** (`std::function` sarmalayici). Fonksiyon isaretcisi
  kayitlari ortuk donusumle AYNEN calisir; capture eden lambda (recipe ureteci) kaydedilebilir.
  TEK sablon kurucu kullan: ayri bir `GenerationPlan(*)(...)` kurucusu capture'siz lambdalarda
  `std::function` donusumuyle BELIRSIZLIK uretir. `registry.generate()` noexcept oldugu icin
  uretecin istisnasi orada plana cevrilir (terminate yok).
- **Dogrulama sirasi §4.4 birebir**: checksums -> manifest -> contract -> parameters -> dialog
  (`validateDialogSchema`) -> recipe (`validateRecipe`) -> altin testler. Tek adim duserse tip
  KAYDEDILMEZ. `checksums.json` varsa 4 cekirdek dosyayi kapsamak ZORUNDA (kismi liste = bypass).
- **typeId cakismasi**: yerlesik veya daha once yuklenmis typeId REDDEDILIR (yerlesik tip paketle
  ezilmez). Ayni typeId iki kokte: ILK kok (proje klasoru) kazanir, ikincisi `shadowed`.
- **Proje klasoru belge yuklenmeden ONCE taranir**: `Document::load` bilinmeyen typeId'yi `orphan`
  isaretler; paket tipleri o an registry'de yoksa geometri korunur ama recompute edilmez.
- **Katalog gorunurlugu manifest'ten**: `catalog.catalogVisible=true` olmayan paket generic
  komut katalogunda GORUNMEZ (kullanici tiklayamaz); fixture'da set edilmis olmali.
- Fixture: `libs/mm_contract/packages/end_plate_pkg` (typeId `mm_end_plate_pkg`, checksums +
  catalog) — Windows kabulu icin `%APPDATA%\ModelMaker\components` altina kopyalanir.
- **Paket dosyalari BYTE-AYNI kalmali: `.gitattributes`'a `libs/mm_contract/packages/** -text`.**
  `checksums.json` HAM baytlarin SHA-256'sidir; Windows'ta `core.autocrlf=true` checkout'u CRLF yapar,
  checksum tutmaz ve paket SESSIZCE reddedilir ("neden katalogda yok" diye saatler kaybedilir).
  Kural eklendikten sonra mevcut kirli checkout KENDILIGINDEN duzelmez: `rm -rf
  libs/mm_contract/packages && git checkout -- libs/mm_contract/packages` ile LF'e zorla,
  `grep -c $'\r' <paket>/manifest.json` = 0 ile dogrula. Yukleyici JSON okurken CRLF gorurse dosya
  adini + cozumu soyleyen ret mesaji vermeli ("... CRLF satir sonu iceriyor (Windows core.autocrlf):
  manifest.json — .gitattributes ... -text gerekli") ve test bunu CRLF'li kopyayla dogrulamali.
- **Acilista (`window_ == nullptr`) yuklenen paketlerin sonucu KAYBOLUR.** ctor'da paket taramasi
  yapiliyorsa `publishStatus` ne durum cubuguna ne de callback'e yazar; kullanici "paket yuklendi mi,
  reddedildi mi" bilgisini hiç gormez. Mesaji uye bir `pendingStartupMessage_`da biriktir, pencere
  olustuktan sonra `flushStartupMessage()` ile goster VE her durumda `model-maker-startup.log`'a
  (`[paket] ...` / `[paket-uyari] ...`) yaz — teshis icin tek kanit bu dosyadir.
- Test: `libs/mm_contract/tests/component_package_test.cpp` (36 kontrol; cakisma reddi, yukleme,
  yuklenen paketin plani == yerlesik `generateEndPlate` DIGEST'i, bozuk paket reddi, golgeleme).
- **Altın test REPLAY'i girdiye bağlıdır; "input" yoksa yalnız YAPISAL doğrulanır.** Yükleyici
  `input` alanı olan goldende recipe'yi koşup `planDigest`'i karşılaştırır (uyuşmazsa paket
  REDDEDİLİR); `input` taşımayan dosya için yalnız name/expect/planDigest tipleri denetlenir ve
  uyarı üretilir. Replay'in gerçekten koştuğunu görmeden (girdiyi kasten bozup paketin reddedildiğini
  doğrulamadan) paketi "altın testli/doğrulandı" diye raporlama.

## Onizleme: veriyle tanimlanan olcu cagrilari + toggles (MM-6)

Amac: bir komponentin canli 3B onizlemesindeki duzenlenebilir olcu cagrilari (callout) ve Boolean
nesne toggle'lari PAKETTEN tanimlanir; widget tipId'ye gore kod yazmaz.

- **Sema**: `ComponentDialogSchema.preview` = `dimensions[ {param, from:"<rol>.<anchor>", to, axis:"u|v|w", label?} ]`
  + `toggles[ {param, rolePrefix} ]`. Codec'e eklenir ama BOS ise JSON'a YAZILMAZ (eski paketlerin
  baytlari/digest'i degismesin).
- **Anchor cozucu CONTRACT katmaninda** (`component_preview.hpp/.cpp`; OCCT yok, Qt yok, Linux'ta
  testli): levha `center/edge_u|v_min|max/face_near|far/corner_*`, bulon
  `center/row|col_first|last/corner_*`, uye `start/end/center/face_top|bottom|left|right`; rol =
  levha/bulon roleKey'i veya `main`/`secondary_N`. Bilinmeyen rol/anchor/bicim `ok=false` + Turkce neden.
- **Olcu ekseninin cercevesi: once `from`, yoksa `to`, yoksa dunya.** `from` bir UYE anchor'i ise
  cerceve yoktur; `to` levha ise onun `u/v/w`'si kullanilmalidir. Yalniz `from`a bakmak ekseni dunya
  Y'sine dusurur ve levha `v` ekseni Z iken olcu SESSIZCE 0.000 cikar ("tasma yok" sanilir).
- **Yukleyici preview'i de fail-closed dogrular**: olcu parametresi semada yoksa, `axis` u|v|w
  degilse, `from`/`to` nokta icermiyorsa, toggle parametresi Boolean degilse TIP KAYDEDILMEZ.
- **Bos tanim, yerlesik callout tablosunu SILMEMELI.** `setPreviewDefinition` bos `dimensions` ile
  cagrildiginda eski (elle yerlesik) callout'lari koru; aksi halde tanim gondermeyen her tip
  (or. Haunch 40) onizlemesini kaybeder. Tanim geldiginde callout'lar tanimdan kurulur: spin ->
  mevcut `onEdit` -> recipe yeniden kosar (davranis ayni), cizim iki anchor'in izdusumu arasina
  olcu cizgisi + uc tikleri + "etiket = deger" koyar.
- **Elle yerlesik callout'lari anchor turevli yerlesime CEVIRMEYI GUI karsilastirmasi olmadan yapma.**
  Anchor turevli konumlar elle secilmis ofsetlerle birebir ayni degildir; "gorunum ayni kalsin"
  sartini Linux testleri kanitlamaz. Guncel kullanimdaki bir komponenti (Haunch 40) veri yoluna
  gecirmek, Windows'ta eski/yeni ekran goruntusu karsilastirmasiyla ayri bir adimdir.
- **Sentinel degerler ekranda GORUNMEZ**: opsiyonel parametre `-1` ("otomatik") veya `<= -1e8`
  ("tanimsiz") tasiyorsa kutu isaretsiz + sema varsayilani gosterilir; motor zaten varsayilani
  uyguladigi icin davranis/digest degismez (yalniz sunum).
- **MM-6(c) TAMAM: toggle'lar VERIDEN gelir (ghost + Alt+tik).** Widget `effectiveToggles()` ile
  `previewDef_.toggles`'i kullanir; bos ise Haunch 40 icin `kStiffeners` fallback (davranis birebir).
  `setPreviewDefinition` BOYUT tanimi olmasa bile `recompute()` etmelidir — yalniz-toggle tanimlari
  (or. berkitmeler) aksi halde ghost'u tazelemez (eski kod `if(!hasDims) return;` ile erken cikiyordu).
- **Haunch 40 generator'u her berkitme icin IKI levha uretir** (`haunch_beam_stiffener` +
  `haunch_beam_stiffener_2`) — ghost sayisi toggle sayisinin 2 kati olur. Testte kesin sayi assert
  ETME; prefix kumesini dogrula (`hasPrefix`).
- **MM-6(d) YARIM: toggle'lar veriye gecti, CALLOUT'lar gecmedi.** `dimensions` semasi
  `{param,from,to,axis}` INTEGER adetleri (nx/ny) ve tek-degerli caplari (M/delik) ifade EDEMEZ;
  Haunch 40'in 25 callout'unun ~10'u (adet + cap) olcu cagrisi degil form alanidir. Tam gecis =
  sema genisletme (integer/value-only editor) + Windows eski/yeni ekran karsilastirmasi (ayri adim).
- Dogrulama: contract testi (anchor noktalari + `kalinlik == t`, `tasma == (h_levha-h_kesit)/2`,
  red yollari, codec round-trip + bos preview yazilmaz) ve Qt dosyalari icin yalniz sozdizimi kapisi
  (`g++ -fsyntax-only $(pkg-config --cflags Qt6Widgets) -I include -I third_party ...`).

## Bulon/uye cakisma + EC3 kontrolu (MM-7)

- **End plate dikey bulon gauge'i KESITE (secHeight) ankrajlanir, levhaya DEGIL.** Yatay gauge zaten `secWidth/2`'den gelir; dikeyin levha yuksekliginden turemesi (eski `(plateHeight - 2*boltEdge)/(ny-1)`) tasan levhada (plateExtra>0) bulonlari BASLIGIN icine iter (IPE300'de delik baslikta kalir). Dikey aralik `(secHeight - 2*boltEdge)/(ny-1)`, "Alt/Ust/Orta" ankraj kesit merkezinden. `EndPlatePlacement`'a `secHeight` ekle; gen==rec icin hem generator hem recipe tasi `endPlatePlacementFromInput` cagirir. boltEdge varsayilani 35 (30 degil): M20/22mm delik + 10mm kaynak payi icin kesit kenarindan ~21mm net mesafe gerekir; 30 yetersiz (30 - tf ~ 16-19 mm).
- **Kontrol fonksiyonu contract'ta (`component_checks.hpp/.cpp`, OCCT'siz).** `runComponentChecks(plan, input, opts)` -> `ComponentCheck{id,pass,value,limit,message}`: delik (d0+kaynak payi 10mm) kiris baslik/govde izdusumuyle cakismamali; e1/e2 >= 1.2*d0, p1 >= 2.2*d0, p2 >= 2.4*d0. `applyChecksToPlan` plan.checks[]'i doldurur, ihlalde plan.ok=false + Turkce mesaj (hangi bulon/uye/mm). Generator VE recipe tasi AYNI `applyChecksToPlan`'i cagirir (gen==rec yapisal garanti).
- **`checks[]` RAPORLAMA alanidir, digest'e GIRMEZ.** codec round-trip'te var (bosken yazilmaz), ama `generationPlanCanonicalJson` once `j.erase("checks")` yapar — digest yalniz GEOMETRI esdegerligini olcer; kontrol mesajindaki degisiklik golden'i kaydirmaz.
- **Fail-closed kontrol mevcut esdegerlik fixture'ini bozabilir.** Buyuk delikli (M24/26mm) + eski kenarli bir fixture yeni kontrol altinda cakisma sayilir (gen.ok=false) ve esdegerlik testi duser. Fixture'i gecerli kil (boltEdge=40 gibi), kontrolu GEVSETME.
- **Haunch 40'ta bu kontrol YOK.** Kendi tutarlilik kontrolleri var (validHaunch40Group: pitch>hole, clear>0; haunchSizing: edge>hole*0.5) ama delik-baslik/govde cakismasi ve EC3 e/p minimumlari yok. Ayni `runComponentChecks` Haunch 40'a ayri adimda eklenebilir.
- **Kontrolun SESSIZ basarisizliga donmesi = yol sapmasi.** `applyChecksToPlan` recipe `endplate.placement` tasinda VE `generateEndPlate` icinde fail-closed calisir (ihlal → plan.ok=false), ama yerlesik End Plate zengin yolunda (`resolveEndPlatePlacement`, application.cpp) CAGIRILMAZ. Belirti tam olarak "paket calismiyor, yerlesik calisiyor" olur. Ayrica basarisizlik yalniz `publishStatus` ile duserse (beep/log yok) kullanici "hicbir sey olmuyor" sanir — bilesen komut yolu (`applyComponentFromParts`/`executePendingComponentCommand`) HER basarisiz dalda `appendStartupLog` + `MessageBeep` + `publishStatus` yapmali. Iki yola (zengin vs recipe/paket) kontrol eklerken IKISINI de dogrula.
- **Varsayilan YATAY gauge otomatik duzeltilir: `max(flans/2, 2.4*d0, tw + 2*kaynak + d0)`, 5 mm'ye yukari yuvarlanmis (MM-7 item 1).** Dar flansli GERCEK profilde (IPE200 b=100, M20 d0=22) eski "flans/2"=50 EC3 p2 (52.8) altinda kalip plani fail-closed reddediyordu; artik otomatik gauge 55 olur ve plan GECERLI kurulur. Kullanici ACIK kucuk `boltSpacingX` verirse DOKUNULMAZ ve kontrol yine reddeder (pitch_p2). Govde acikligi terimi `tw + 2*kaynak + d0` delik-govde cakismasini onler; `EndPlatePlacement.secWebThickness` + `computeEndPlatePlacementOnFace` parametresiyle tasinir (gen==rec korunur). Levha genisligi varsayilani en az `gauge + 2*(1.2*d0)`. Golden fixture'lar SAHTE profil kullanabilir (orn "IPE200" aslinda 200×400) ve bunu maskeler; "varsayilan artik gecerli" iddiasini GERCEK dar-flans profiliyle (`component_checks_test` 4b: spacingX==55) dogrula, golden'a degil.
- **Egik kiris reddi YALNIZ alin levhasina ozgudur; paylasilan `resolveShearTabPlacement` icine KOYMA.** `endPlatePlacementFromInput` kirisin kolon yuzune DIK gelmemesini (tegetsel bilesen min|aW|,|aH| veya kolon ekseni boyunca bilesen > sin5°) ACIK reddeder. Haunch 40 egik kirisi `haunchContactPlacement` uzerinden destekler; kontrol paylasilan fonksiyona konursa Haunch 40 bozulur.

## Otomatik onizleme tanimi (MM-6b) — duyarlilik analizi

- `autoPreviewDefinition(generate, baseInput, specs)` plan(p) vs plan(p+δ) (δ=1mm/1deg/adet+1; aralik ust sinirindaysa -δ) farkindan olcu uretir. Length/Angle **ve Integer (adet)** -> dimensions; Boolean -> toggles (plan(true) vs plan(false) rol farki, ortak onek -> rolePrefix); fark uretmeyen sayisal parametre -> ineffective[].
- **Dogrulama kontrolu (MM-7) duyarlilik analizini SESSIZCE oldurur: plan.ok=false olsa bile GEOMETRI varsa karsilastir.** Varsayilani 0 = "otomatik" olan bir parametre (or. `boltSpacingX`) +1 mm perturbe edilince EC3 aralik sarti ihlal edilir, `applyChecksToPlan` plan.ok=false yapar; eski kod bu perturbasyonu "etkisiz" sayip atiyordu ve satir/sutun ucu olculeri (col_first/col_last, row_first/row_last) HIC uretilmiyordu. Kural: `if (!pertPlan.ok && pertPlan.plates.empty() && pertPlan.boltGroups.empty())` -> etkisiz; aksi halde geometri karsilastirilir (analiz URETIM degil). Testte bu vakayi (varsayilani 0 olan aralik parametresi) acikca kur.
- **ADET (Integer) parametrelerini atlama.** Dongu yalniz Length/Angle geziyorsa `boltCountX/Y` gibi adetler hic analiz edilmez; adette adim 1'dir ve `hasMaxInt/maxInt` kelepcesi uygulanir. Adet degisimi bulon izgarasinin uclarini oynattigi icin gercek olcu uretir.
- **Levha geometrisi testi yazmadan ONCE builder'in taban noktasini oku: kalinlik `center` etrafinda SIMETRIKTIR.** `buildPlateSolid` taban yuzunu `center - axis*t/2` noktasina kurup `+axis*t` extrude eder; levha z aralığı ±t/2'dir, "center'dan +axis'e t" DEGILDIR. Bu varsayimla yazilan test 14 kontrolu birden dusurur ve `hasPoint` "yok" kontrolleri TRIVIAL olarak gecer (hicbir nokta eslesmedigi icin "pah kosesi kaldirildi" yanlis yesil olur). Testte hem + hem - yuzu, hem de "nokta YOK" iddialarini ayni vakada dogrula.
- **Bilinen kisitlar:** (a) yalniz eksen boyunca oteleyen (gap) veya yalniz display/param degistiren (boltDiameter/holeDiameter) parametreler diff'te gorunmez -> yanlis "etkisiz"; merkez/param farki da izlenmeli. (b) Integer/Enum/Material/String "etiket"i icin PreviewDefinition'da alan yok — ayri sema genisletmesi gerekir (Integer ARTIK analiz ediliyor, yalniz etiket alani yok). (c) ayni kenari hareket ettiren iki parametrede (topExtra vs upperEdgeDistance) "en yakin hareketsiz anchor" referansi elle yazilan tanimla birebir eslesmeyebilir; yan yana ekran karsilastirmasi gerekir.

## PlateSpec cercevesi — el yonu ve kose numaralama (CONTRACT kurali)

- `{center, u, v, axis}`: genislik `u`, yukseklik `v`, kalinlik `axis` boyunca **simetrik (±t/2)**; extrüzyon yonu `+axis`.
- Kose numaralari MATERIALIZER nokta sirasina gore: `0=(-u,-v) 1=(+u,-v) 2=(+u,+v) 3=(-u,+v)`; `PlateSpec::Chamfer::corner` bunu kullanir, `h` bacagi `u`, `v` bacagi `v` boyunca.
- **El yonu SERBEST**: `u × v = +axis` zorunlu degil; sol elli cerceve (`u × v = -axis`) AYNI formulle yorumlanir, materializer cerceveyi "duzeltmez" (eksen takas etmez, `v` isaretini cevirmez). Sol elli levhayi tuketen taraf (Studio) sag elli yerel ekseni `x=u, y=-v, z=axis` alip pah kosesini `3-corner` yeniden numaralandirir; dunya koseleri ve `generationPlanDigest` ayni kalir.
- Regresyon: `tests/plate_frame_handedness_test.cpp` (sag + sol elli, kalinlik aralığı, pah kosesi). Yeni bir levha tuketicisi yaziyorsan bu testi kopyalayip kendi cerceve kuralini sabitle.

## Concurrent / yetim surec

- **Ayni host'ta baska bir Hermes oturumu ayni branch'e ayni anda commit atabilir (overnight run).** Is oncesi `git log --oneline -1` + `git fetch origin-ssh` ile ucun degismedigini dogrula; `pgrep -af 'cmake --build|ctest'` ile yetim derleme sureci olup olmadigini gor (3.7GB host'ta RAM yarisi yaratir). Yetim surec isi zaten bitirmis olabilir — `git status --short` temiz mi bak, duplication'a girmeden once.

## Review discipline

- Independent review via `delegate_task`; the config pins `delegation.provider=openai-codex,
  model=gpt-6-sol`, so a spawned reviewer inherits that model — confirm with the returned
  `model` field before trusting it. Read all findings, fix Critical+Major, re-test, commit local.
- Git: small logical LOCAL commits only; no push/merge/force-push unless the user explicitly says so.
  (In this user's overnight branch flow the user expects each finished slice pushed to the feature
  branch.) Chain build -> test -> commit -> push with `&&`, never `;`: a failing test followed by `;`
  pushes a red commit.
- **Bundle handoff: RUN `git bundle create`, then `git bundle verify` + `ls -la` BEFORE telling the
  user it is ready.** The Windows handoff is `git bundle create <file> <branch>` → `scp` →
  `git fetch <file> <branch>` + `git merge FETCH_HEAD`. A create that was only PLANNED (never run)
  leaves no file on disk, so the user's scp/fetch fails "does not appear to be a git repository"
  and you burn a full round-trip. Also confirm the bundle's SOURCE checkout with `git rev-parse
  HEAD` before patching a file — this repo has a second stale checkout whose HEAD differs from the
  bundle line; patch the checkout the bundle is built from. After recreating a bundle with new
  commits, the user's previously-downloaded bundle of the SAME NAME is stale — re-cloning it
  silently checks out an older commit (the tip drifted) and the new feature is absent. State the
  byte size and the expected tip SHA in the handoff (`ls -la` for size + `git log --oneline -1`
  after clone as the checkpoint) and tell them to re-download/re-clone if the size differs.
- **Windows Git Bash (UCRT64/MSYS2): `scp -i` needs the Unix-style `/c/...` path, never the `C:`-drive form.** A `C:`-drive path is read as an option/filename, so scp fails 'Identity file not accessible: No such file or directory' and then falls back to default keys → 'Permission denied (publickey)'. Give the key path as `/c/Users/<user>/<key>.pem`; on a 'UNPROTECTED PRIVATE KEY FILE' warning, `chmod 600` the key first.
- **Windows kopyasını güncelleme: origin GitHub HTTPS olmalı; kirli ağaçla `git pull` "no tracking information" ile düşerse `git checkout -- .` ÖNERME — commit'lenmemiş değişiklikleri SİLER.** Bundle handoff emekli; Windows klonunun origin'i `https://github.com/sbabadag/model-maker.git`. Upstream'siz bir dalda çıplak `git pull` "There is no tracking information for the current branch" ile düşer ve eski checkout'tan kalan modified tracked dosyalar fast-forward'i bloklar. Kurtarma: `git remote -v` ile origin'in GitHub olduğunu doğrula → `git status --short` (önce değişiklikleri GÖSTER) → `git pull --ff-only origin <branch>` → `git log --oneline -1` ile beklenen SHA'yı doğrula. Dalı `origin <branch>` ile AÇIKÇA ver; çıplak `git pull` dalı tahmin etmez.
- **Never hand the user a command containing a placeholder.** Resolve the real value yourself first
  (`curl -s ifconfig.me` for the dynamic AWS IP, `git rev-parse --show-toplevel` for the repo) and
  substitute it into the command. The user pastes the line verbatim, so `<IP>` yields
  `-bash: IP: No such file or directory` and the follow-up fetch reports "does not appear to be a git
  repository" — two failed round-trips for a value you already know. Give multi-machine handoffs one
  Give multi-machine handoffs one command per block (the user copies blocks one at a time), and state that scp must succeed before fetch runs. NEVER chain commands with `&&` in a handoff and never put two commands on one line — the user pastes the block verbatim, so `scp ... && git fetch ...` or `cmake -S . -B build-occ cmake --build ...` (two commands concatenated) fails and costs a round-trip; number each command on its OWN line and say "run them one at a time". When the user asks "give them on separate lines", they mean literally one command per line, not `&&`-joined.
  **Aciklama cumlesini KOMUT SATIRINA karistirma.** "cd /c/work/mm-p2d ve build-occ\model-maker.exe" gibi bir satir yapistirilinca bash `cd: too many arguments` verir; "exe build-occ kokunde" gibi ipucu vermek yetmez cunku kullanici satiri AYNEN yapistirir. Komutun kendisi tek basina calisir olmali; aciklama ayri cumlede. Windows/MSYS bash'te **ters bolu kullanma** (`./build-occ/model-maker.exe`, `\` degil) — bash ters boluyu yiyip `build-occmodel-maker.exe: command not found` uretir. Bir yapistirma basarisiz olursa kullaniciyi suclamadan once komut metnini oku: hata mesajinda komut adi birlesmis gorunuyorsa (`build-occmodel-maker.exe`) sorun satirdaki aciklama/ters boludur, ortam degil.
  fetch runs.

## Weld & Assembly GUI (Phase W2) — integration rules

- **Open the weld PARAMS dialog BEFORE entering pick mode, never inside the Win32 mouse handler.**
  The clean flow is: menu action → `QDialog::exec` (Qt event loop, safe) → `beginWeldCommand(type,size,length,cls)`
  → user picks two parts → auto-create on the 2nd pick. Opening a modal Qt dialog synchronously
  from inside `handleCanvasMessage`/`toggleModelSelection` re-enters the event loop mid-Win32-message
  and corrupts selection state. This also matches the acceptance test ordering ("Weld → Fillet → 5mm → Shop" = params first).
- **Assembly-mode selection needs its OWN replace/add method, not a post-hoc expansion of `toggleModelSelection`.**
  `toggleModelSelection` TOGGLES the clicked part (plain click does not replace the whole selection),
  so wrapping it in `expandSelectionToAssembly()` after a plain click accumulates assemblies instead of
  replacing. Write `selectAtAssemblyMode(x,y,additive)` that resolves the hit, expands via
  `document_.assemblyMembers(partId)`, then REPLACES (plain click) or ADDS (Ctrl+click) the member
  index set — and call it from BOTH neutral click sites (Draw2D + View3D) and the Ctrl+click path.
- **A new overlay glyph lambda in renderer.cpp must be invoked at BOTH draw call sites.**
  `draw()` has two overlay blocks: the motion/interactive frame (~line 1116) and the full frame (~line 1946).
  Adding `drawWeldGlyphs()` only to one makes the weld symbol flicker/disappear during navigation.
- **Weld glyph: skip the default-origin sentinel.** A Weld default-constructed with origin (0,0,0) and
  direction (1,0,0) has no computed position — skip drawing it (and its hit-test) or a phantom glyph
  appears at the world origin. The GUI `weldSelectedParts` computes origin from the two parts' bbox
  centres, so only GUI-created welds render.
- **`weldSelectedParts` computes origin/direction/length from `document_.modelBounds()` bbox centres.**
  `identification` is NOT auto-assigned by `Document::addWeld` — assign "W001".. from `welds().size()+1`
  in the caller, or welds appear nameless in the tree.
- **Component (146+) welds: the generator emits `WeldSpec`, the materializer resolves roleKeys.**
  `partA` = the user part (EntityId known at generation), `partB`/`partBRole` = an owned child (EntityId
  assigned only at commit). `componentPlanFromGenerationPlan` turns `plan.welds`/`weldKeys` into
  `ComponentObjectKind::Weld` children, and the kind-priority sort places Weld LAST so plate EntityIds
  exist when `resolveWeldPart` runs. Geometry is committed via `doc.addWeld(w, wf)` +
  `store->set(displayEntityId, solid)` + `recordGeometryDelta`; on failure the record is kept as
  metadata with `unsupportedReason`. Dialog weld rows reach the generator as EXTRA params
  (`createWelds`/`weldSize`) — `alignParamsToSpecs` preserves parameters absent from the schema, so
  never add them as schema params just to make them survive.
- **A derived user-catalog type ("... - Özel") is NOT a separate code path.**
  `UserCatalog::registerInto` re-registers the FAMILY generator (`registerType(t, *gen)`) and COPIES
  the dialog schema (`d.schema = src->schema`), so weld rows and tab layout survive and the weld
  behaviour is identical to the family. Debug the family generator; do not go looking for a
  derived-type bug. EXCEPTION — dialog fields: `d.specs` comes from the template's SAVED spec list
  (written by an older build), not from the family. Any field added to the family dialog later
  (e.g. `documentedOnly` Tekla fields) is missing, and tabs made only of such fields vanish. When
  a user's screenshot shows fewer tabs than the code defines and the title ends in "- Özel", merge
  the family's `documentedOnly` specs into the derived dialog (done in `registerInto`) and test it
  with a template whose saved specs predate the new fields.
- **When a weld produces no solid, report the GAP in millimetres before changing anything.**
  `buildWeldSolids` appends the `BRepExtrema_DistShapeShape` minimum distance to
  `unsupportedReason`: ~0.5-2 mm means a tolerance/junction problem, ~100-150 mm means a
  placement or section-dimension error (see the anchor pitfall above). Surface it through
  `solidStatusMessage_` on BOTH the create path and the dialog-OK recompute path, and ask the user
  for that string instead of guessing. A profile-less member (Properties shows
  "-- Profilsiz (düz çizgi) --") has no section dimensions, so `memberGeometryFor` falls back to
  200x300 + offset 0 and the contact point can miss the real solid — check that first.

## Windows validation on the Linux host (syntax-check, not a real build)

The GUI files (`application.cpp`, `qt_main_window.cpp`) are WIN32/Qt6-only and cannot compile on
Linux. Before committing changes to them, syntax-check with mingw using the REAL CMake defines — a
bare invocation produces ~15 false `LoadCursorW`/`ListView_SetItemText` wchar-vs-char errors
because `UNICODE` is undefined. Recipe (from CMakeLists.txt lines 94/183/186):

```
x86_64-w64-mingw32-g++ -std=c++20 -fsyntax-only -I include -I /tmp/qstub \
  -D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00 -DNTDDI_VERSION=0x0A000003 \
  -DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN -DNOMINMAX -DQT_NO_ENTRYPOINT \
  src/application.cpp
# OCC-on adds: -I /usr/include/opencascade -DMM_HAS_OCC -D_USE_MATH_DEFINES
```

- `_USE_MATH_DEFINES` is REQUIRED for OCCT headers or every `gp_*.hxx` fails with `M_PI not
  declared` — a stub-environment artifact, not a real code error.
- `qt_main_window.cpp` CANNOT be checked directly: it needs ~60 Qt6 headers and the stub dir only
  covers QComboBox/QString. Linux has real Qt6, so for a new block there: extract the block
  (slice between two unique marker lines) into a throw-away TU with a FakeApp/QMainWindow
  subclass, then `g++ -fsyntax-only $(pkg-config --cflags Qt6Widgets)` against the real widget
  headers (script in the scratch dir). Also verify each `app_.method()` is declared in
  `application.hpp` (the application.cpp mingw check proves it compiles).
- This is a syntax gate only — never report "Windows build passed" from it. A real Windows+Qt+OCCT
  compile is a separate, user-side step.
- **`-fsyntax-only` is SYNTAX ONLY — it does not link, so it will NOT catch a missing definition.**
  The classic trap: a function declared + defined INSIDE the big `#ifdef MM_HAS_OCC` block of
  `application.cpp` but CALLED from OCC-off code. The OCC-off syntax check compiles the call site and
  the header declaration cleanly; only the real Windows OCC-off LINK fails with
  `undefined reference to mm::Application::<fn>` for a function that `search_files` shows IS defined. Fix:
  define portable functions OUTSIDE the MM_HAS_OCC block, and for OCC-needing functions keep their own
  inner `#ifdef MM_HAS_OCC ... #else ... return false; ... #endif` so the OCC-off build links a stub.
  Linux never catches this — `application.cpp` is WIN32-only and is never compiled or linked on Linux
  (only core + tests build), so the full-app link is a Windows-only gate. Symptom surfaced here:
  `selectedComponentInstance` / `setSelectedComponentParams` sat inside the 5216→5684 OCC block and
  the Asus OCC-off link failed on both.
- **When mechanically moving a function with a script, match the closing brace at COLUMN 0** (line
  exactly `}`), not `line.strip() == "}"` — the latter also matches indented inner-block braces and
  half-extracts the function, corrupting the file. `git checkout` the clean file and redo the slice
  rather than hand-fixing a corrupted extraction.

- **Dosya düzenlemesini `write_file`/`patch` ile yap; heredoc ve `execute_code`'tan KAÇIN.** Gözetimsiz/api oturumunda (onayı cevaplayacak kullanıcı yok) `execute_code`, `python3 - <<'PY' …` ve birçok `rm -rf`/`pkill` çağrısı "approval withdrawn / did not run" ile BLOKLANIR ve hiçbir şey yazılmaz/çalışmaz. Yeni dosya için `write_file`, düzenleme için `patch` kullan; silmek yerine YENİ bir çıktı/scratch dizin adı seç (`rm -rf build-occ` yerine `build-occ2`) ki bir derleme yıkıcı bir komutun geçmesine bağlı kalmasın. Terminali yalnız build ve test koşuları için kullan.
  Bu kural COMMIT komutları için de geçerli: `python3 - <<'PY' … PY` + `git add/commit` AYNI komutta
  birleştirilirse bloklanır ve HİÇBİR ŞEY çalışmaz (commit atılmaz, dosya yazılmaz). Bloklanan
  komutu tekrar DENEME; `write_file`/`patch` ile düzenle, sonra sade `git add <yollar>` +
  `git commit -q -F -` (ya da `-m`) ile ayrı turda commit et.
- **Tek dosya birden çok maddeye hizmet ediyorsa (CONTRACT.md, ortak CMakeLists) maddeleri AYRI commit'lere böl:**
  her commit'te yalnız o maddenin dosyalarını `git add <yol> <yol>` ile sahne al; ortak dosyadaki
  GELECEK maddeye ait bloğu `patch` ile geçici olarak çıkar, commit'i at, sonra bloğu geri koy.
  Etkileşimli `git add -p` kullanma (gözetimsiz oturumda cevap veren yok). Sıra: bağımsız kod/test
  commit'leri → paylaşılan doc commit'i → bağımlı test commit'i; commit mesajında hangi testin kaç
  kontrolle geçtiğini yaz ki sonraki oturum doğrulamayı tekrar koşmak zorunda kalmasın.
- **Cok satirli bir `patch` bir dosyayi bozarsa (ciktida literal `\n`, satirlar tek satira biner), `git checkout -- <dosya>` ile HEAD'e dondurup MINIMAL tek-satirlik bir patch ile yeniden uygula.** Buyuk cok satirli yorum bloklari eklemek yerine yalniz eklenmesi gereken 1-2 satiri hedefle; boylece bozulma tekrarlanmaz ve geri alma kolay kalir.
- **Bir `patch`in `old_string`i bir fonksiyon imza satirini ICERIYORSA, `new_string`de de aynen yaz.** Ekleme yaparken imza satirini (`QPointF X::project(Vec3 p) const {`) yeni metne koymayi unutmak, dosyayi sessizce gecersiz kilar (`expected unqualified-id before 'const'`); hata bir sonraki derlemede "bilinmeyen" satirda cikar. Patch'ten hemen sonra o TU'yu `-fsyntax-only` ile derleyip diff'e bak.

See `references/component-builder-api.md` for the fluent API and relationship kinds.
See `references/tekla-tab-functionalization.md` when a greyed Tekla tab must become fully
functional (field promotion order, hole/slot/stagger/cut-length technique, icon-reading fallback).

## Linux OCC-on validation (component/materializer tests)

On Linux the CMakeLists has a MANUAL OCC branch (`find_path(OCC_INCLUDE_DIR ...)` +
`find_library(... TKernel ...)`) that never calls `find_package(OpenCASCADE)`, so with system
OCCT installed (`/usr/include/opencascade`, `libTKernel.so`) `MODELMAKER_OCC_READY=TRUE` and the
OCC tests ARE built into the normal build dirs — `build-occ-hermes` already contains
`model_maker_component_command_path_test` etc. Build and run the SPECIFIC target directly:
`cmake --build build-occ-hermes --target model_maker_component_command_path_test` then run the
binary. Do NOT assume these tests are missing and jump to a one-liner first — check
`grep -c <testname> build-occ-hermes/build.ninja` before hand-rolling a g++ line. The one-liner
fallback (only when OCC is disabled or the target is absent):
`g++ -std=gnu++20 -O1 -DMM_HAS_OCC -I include -I /usr/include/opencascade <test>.cpp <core sources> -lTKernel -lTKMath -lTKG2d -lTKG3d -lTKGeomBase -lTKBRep -lTKGeomAlgo -lTKTopAlgo -lTKPrim -lTKBO -lTKBool -lTKShHealing -lTKMesh -lTKXSBase -lTKDESTEP -o <bin>`.

- Keep the source list in ONE shell variable reused by every test; the component tests need the
  document/codec/geometry/materializer set, and `component_materializer_test` additionally needs
  `src/occ_bridge.cpp`.
- **Adding a core `.cpp` means editing that list too.** A new core source (a collector, a codec)
  is invisible to these one-liners until appended — the symptom is `undefined reference` at link
  time for a function that plainly exists and passes in the CMake-built core tests.
- **`tests/run_occ_solid_ids.py`'nin `CORE_SOURCES` listesi de elle tutulur.** Uretim govdesi (or. `generateEndPlate`) baska bir contract dosyasindaki fonksiyonu cagirmaya baslarsa (or. `endPlateFromParams` -> `component_dialog.cpp`, o da `applyShearPlate146Canvas` -> `component_dialog_146_canvas.cpp`), o dosya + transitif bagimliliklari `CORE_SOURCES`'a ekle; yoksa Linux ctest'inde `model_maker_occ_solid_ids` `collect2: error: ld returned 1 exit status` + 'HARNESS HATASI: derleme BASARISIZ' ile duser. Ayni kural diger `run_*.py` harness'lerinin (gumball, two_point_view, multi_viewport) manual source/symbol listeleri icin gecerli.
- Run the binaries directly in a console, not through a pipe: ctest/pipe buffering swallows the
  PASS/FAIL lines if a real-OCCT test segfaults, which reads as "no output" instead of a crash.
