# Render pipeline audit (solid display, silhouette, GDI vs GL)

Three DIFFERENT things are easy to conflate; keep them separate:

1. B-Rep (`TopoDS_Shape` in OccSolidStore) - truth, never touched by rendering.
2. Display mesh = `WireframeModel.faces()` triangles (GPU/GDI fill). Since the audit every
   OCC-built model also carries `faceGroups()` (per-triangle B-Rep face id, bit31 = curved).
3. Lines: REAL edges (`model.edges()`, B-Rep boundaries, crease-filtered) vs SILHOUETTE (curved-face
   mesh edge between a front and a back triangle) vs INTERNAL tessellation edges (never drawn).

## Facts that cost time
- OCC triangles do NOT honour face orientation: `TopAbs_REVERSED` faces must swap two nodes
  (35/124 triangles were reversed on a plate with one hole). Fixed in `shapeToWireframeWithFaces`.
- Old contour code took `faceReference` per face, but OCC faces are single triangles -> `front` was
  always true -> silhouette was ALWAYS empty (GL and GDI). Silhouette must come from consistently
  oriented neighbour normals: `solid_topology.{hpp,cpp}` (`collectSilhouette`, BFS orientation
  propagation, so even mixed-winding meshes work). Camera is orthographic: towardViewer = rotation
  matrix row 2 (`R[6..8]`); silhouette depends ONLY on that, not pan/zoom -> cache key = rotation.
- Silhouette candidates = `smooth` set only (curved-face interior + tangent faces < 20 deg). Hard
  creases are already in `model.edges()`; adding them as silhouette double-draws lines.
- `model.edges()` curved edges are 48-segment polylines, NOT mesh edges: vertex-index matching
  between edges() and triangle edges fails (physical=40 of 424). Never classify by index identity.
- Tekla Bridge solids are sewn triangles (each triangle = a face): facet edges become 'real' edges
  at the 2.56 deg crease default. `shapeToWireframe*(..., creaseAngleDeg)`; the bridge import passes 25.
- GDI path problems (pre-audit): per-triangle brush+Polygon, NULL_PEN seams, painter's sort wrong for
  interpenetrating solids, transparent style AlphaBlends each triangle (double-darkening seams), all
  edges drawn without depth test (HiddenLine showed back edges). Fix = `soft_raster` + `scene_raster`
  (portable, 28.4 fixed point, top-left rule, z-buffer, 2-layer transparency, depth-tested lines),
  wired in `Renderer::draw`; `MM_GDI_LEGACY_FACES=1` restores the old path, `MM_RASTER_LOG=1` logs
  stage timings and GL glFinish/glReadPixels averages.
- HLR vs depth buffer (measured, repro scene 8/32/72 parts): OCCT PolyAlgo 18/75/203 ms, exact Algo
  36/158/446 ms, per VIEW DIRECTION; software z-buffer 4 ms for 8 parts, 33 ms for 88k triangles.
  HLR scales superlinearly and must rerun on rotate -> depth buffer chosen.

## Tools
- `tests/render_scene.hpp` repro scene (HEB300 + 2 IPE300 + 2x Tekla 146); `tests/render_audit.cpp`
  (counts), `tests/render_bench.cpp` (old-vs-new, HLR; build by hand with TKHLR),
  `tests/occ_render_visual_test.cpp`, `tests/soft_raster_test.cpp`, `tests/solid_topology_test.cpp`.
- Raw-mesh trick for 'is the fix needed': mesh with BRepMesh yourself WITHOUT the orientation swap and
  assert `flippedTriangles > 0`.

## PERF-001 (CPU raster olcekleme, worktree /home/ubuntu/model-maker-perf001, dal perf-001-render-scale)
- Bench: `model_maker_perf001_bench`; A/B: ~/.hermes/cache/scratch/perf001/run_ab.sh (bench_baseline vs bench_new, ARA ARA). Paylasimli 2-vCPU'da tek-tekrar ±%30 gurultu: 5 tekrar + 'best-of' orani kullan; N>=1000 tek tekrar kanit degil.
- fillClipped: satir basina tam tamsayi aralik taramasi buyuk ucgende 2-5x kazandirir AMA satir basina 3 int64 bolme kucuk (<16 px) ucgende YAVASLATIR (N=300 iso 0.61x) -> hibrit (dar kutu eski piksel testi). Dogrulama: tests/soft_raster_diff_test.cpp (tests/perf_ref/ dondurulmus 9e458bd kopyasi; piksel+z bit esitligi).
- Camera::project/viewTransform ayri TU'da: rasterizeScene'de ayni islem sirasiyla inline (bit-ayni).

## Not done / unverifiable on Linux
- Secondary-viewport OpenGL (one hidden-window context + FBO that is re-created on every size change,
  batch cached per document revision: sharing it across viewports would thrash both). Needs per-view
  FBO + shared batch with slab filter, testable only on Windows.
- Real GDI BitBlt/DIB timings and screenshots of the Windows window.
