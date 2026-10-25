# Tekla 146 (Shear plate simple) — validation report

Source: https://support.tekla.com/doc/tekla-structures/2026/conn_146_help
Retrieved: 2026-10-19T22:30:59Z · Page modified 16 Jan 2025 · Tekla Structures 2026

## 1. What 146 is

Shear plate simple (146) connects a beam to a beam, or a beam to a column, with a
single square shear tab or double shear tabs. The shear tab is **welded to the main
part web and flanges** and **bolted to the secondary beam web**. The secondary beam can
be leveled, sloped and/or skewed. An opposite-web stiffener plate is optional.

Objects created: shear tab (1 or 2), stiffener (optional), haunch plates (optional),
weld backing bars (optional), seat angles, welds, bolts, cuts.

Selection order: 1 = main part (column or beam), 2 = secondary part (beam).

## 2. Comparison with existing model-maker fin plate

The existing `fin_plate` component (typeId `fin_plate`) already implements the
**single shear tab, orthogonal, beam-to-column** case:

- shear tab welded to the column face, bolted to the secondary beam web;
- plate plane parallel to the beam web (thickness = section width direction);
- role detection by verticality (selection order irrelevant);
- nearest-end selection (tab never lands on the far end);
- column-face support function (tab sits on the column face, not the centreline);
- SectionAnchor offset (tab at the beam WEB centre, not the top flange);
- t-eccentricity (plate face t/2 off the work line, centre at t);
- bolts drill {plate, secondary web}, NOT the column (weld connection).

These are exactly the semantics 146 documents for the orthogonal single-tab case.
Therefore the 146 type **reuses this proven core verbatim** — it is not a parallel
engine, and the fin plate production path is untouched.

## 3. What the 146 slice implements (verified)

- `tekla_shear_plate_146` component type registered in `ComponentRegistry`.
- Generator delegates to the fin plate core (single orthogonal shear tab).
- Param schema is a superset: the 8 core shear-tab/bolt params are honoured; the
  remaining 40 documented params are carried in the schema (never silently dropped).
- Dialog definition (`shearPlate146DialogDefinition`) with 146 tab structure
  (Picture / Plates / Bolts / Stiffeners) + schematic + hotspots.
- Tests: schema validation, param coverage, roleKey stability, default geometry,
  param variation, End Plate + Fin Plate regression.

## 4. Supported / unsupported / unresolved

### Supported (8 params)
plateThickness, plateWidth, plateHeight, boltDiameter, holeDiameter, boltCount,
boltEdge, grade.

### Documented, not implemented (this slice)
beamGap, flangeStrip, beamEndCut, beamWebCut, beamFlangeCut, beamBottomFlangeCut,
shearTabPosition (double tab), tabGapSecondary, shearTabEdgeDistance, shearTabEndCut,
tab chamfers, shearTabOrientation, plateWasher*, stiffener*, haunch*, notch*,
boltHorizontalPos, boltVerticalPos, boltVerticalMeasure, boltSpacing, deletedBolts,
staggering, boltGroupOrientation, boltingDirection, holeType, backingBar*, weldAccessHole*,
beamEndPrep.

### Unsupported (kernel limitation)
seatAngleProfile (profile catalog), seatAngle* (angle section + stiffener types),
BCSA notch, weld access hole shapes (no CutModifier primitive).

### Unresolved (external data)
Exact default bolt spacing / AutoDefaults (joints.def).

## 5. Verdict

Status: PARTIALLY_IMPLEMENTED. The orthogonal beam-to-column single shear tab is
geometry-verified (existing fin plate e2e locks the placement). The remaining 146
surface is preserved in the blueprint and coverage report, not fabricated. Not
PRODUCTION_READY: beam-to-beam, double tab, stiffener, haunch, seat angle and the
cut/notch family are still open.
