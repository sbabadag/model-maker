# ComponentBuilder fluent API + relationship kinds

`include/model_maker/component_framework.hpp` (saf veri modeli, OCC'siz; Linux'ta birim testli).

## ComponentBuilder — object methods (each takes a `roleKey` first)

```cpp
ComponentBuilder b;
b.plate("fin_plate", plateSpec)         // PlateSpec{center,u,v,axis,width,height,thickness,name}
 .boltGroup("beam_bolts", boltGroup)     // BoltGroup (component_engine.hpp)
 .cut("beam_fit", cutModifier)           // CutModifier (cut_modifier.hpp)
 .weld("fin_weld", weldSpec)             // WeldSpec{partA,partB,throat,name}
 .point("ref_pt", pos)
 .line("ref_line", origin, dir)
 .plane("ref_plane", origin, normal);
```

## ComponentBuilder — relationship methods

```cpp
 .align(a, b, offset)         // Vec3 offset
 .faceContact(a, b)
 .offset(a, b, distance)
 .coincident(a, b) / .parallel(a, b) / .perpendicular(a, b)
 .followFrame(a, b)
 .sizeFromMember(obj, member)   // member = main / secondary_N / a roleKey
 .sizeFromParameter(obj, param) // param = CompParamSpec name (to is NOT a roleKey)
 .attachedTo(a, b)              // dependency edge a -> b
 .cuts(cutter, target)          // target MAY be a user part
 .welds(weld, partA, partB)
 .ownedBy(child, owner)         // owner must be "component", never main/secondary_N
 .dependsOn(a, b)
```

`build()` returns `Result{ok, objects, relationships, error}`. It fails (ok=false) on:
invalid/duplicate roleKey, unresolved reference, Parallel+Perpendicular conflict on the same
pair, a dependency/ownership cycle, or `ownedBy` targeting a user part. Fail-closed: a bad build
never reaches the materializer.

## RelationshipKind families (ComponentRelationshipGraph)

- geometric (undirected): Coincident, Offset, Parallel, Perpendicular, FaceContact, Align,
  FollowFrame, SizeFromMember, SizeFromParameter.
- dependency (directed): AttachedTo, DependsOn.
- ownership (directed): OwnedBy, Cuts, Welds.

Cycle detection runs DFS only over dependency+ownership edges (directed). `validate()` returns
`Ok | Unresolved | Conflict | Cyclic` (Ambiguous is reserved, not yet produced by validate).

## roleKey rules

- Charset `[A-Za-z0-9_]` only; non-empty; unique within a component.
- Reserved: `main`, `secondary_0..N` (user parts), `component` (the instance itself).
- `SizeFromParameter` target is a parameter name, exempt from roleKey lookup; its `from` is always
  a real object/roleKey.

## Materializer contract (concrete, MM_HAS_OCC — `component_materializer.hpp/.cpp`)

The old abstract `materialize(objects, ...)` interface is GONE; the materializer is now a
concrete class with a two-phase prepare/commit plus recompute:

```cpp
ComponentMaterializer mat(doc);                 // holds Document& + OccSolidStore*
mat.prepare(plan, mainId, secondaryIds) -> Prepared{ok, children, error}
mat.commit(prep, record)      -> CommitResult{ok, roleToEntity, componentId,
                                              ownedPlateIds, ownedBoltGroupIds, error}
mat.recompute(prep, existing, record) -> CommitResult  // reuses existing roleKey→EntityId
```

- `prepare` generates + validates real OCCT geometry and does NOT mutate Document. It fails
  (ok=false) on: Weld (no weld motor — explicit Unsupported), `Parallel`/`Perpendicular`
  orientation constraints (no OrientationConstraint solver), a dependency cycle (delegated to
  `ConstraintSystem::solve`), or an unresolved child dependency.
- `commit` applies everything under ONE undo record and rolls back via `rollbackOpenRecord` on
  any failure; plates materialize before bolt groups (kind-priority sort) so `bolt.parts` gets
  the owned plate IDs.
- `recompute` keeps the roleKey → EntityId map STABLE: `replaceModel`/`setBoltGroup` (never
  addModel/addBoltGroup), so a moved beam re-resolves the SAME plate/bolt EntityIds.
- The roleKey → EntityId map persists into `ComponentInstanceRecord.children` and is serialized
  in the MMW8 'N' block (forward-compatible; empty children on a legacy file = no map).
  Generation payload (PlateSpec/BoltGroup) is NOT re-serialized — the registry generator
  reproduces it deterministically (`componentPlanFromGenerationPlan` assigns plate_i/bolt_group_i/…).
