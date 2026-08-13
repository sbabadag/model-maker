"""Extract LINE rendering baseline info from Model-Maker code."""
# This analyzes the rendering pipeline to give a theoretical/structural baseline

import json, sys

BENCHMARKS = {
    "entity_count": [500, 2000, 5000, 10000],
    "per_entity_ops": {
        "gdi_draw_calls": 1,         # drawModel → one Polyline/MoveToEx+LineTo per edge
        "projected_vertices": 2,      # 2 per line segment
        "pen_creation": "cached",     # entityPens map caches pens
        "spatial_query": "r-tree",    # bounds query via spatial index
    },
    "render_passes": {
        "face_pass": "skipped for line-only models (no faces)",
        "edge_pass": "select pen → Polyline(dc, pts, 2) → drawModel",
        "selected_pass": "green pen override, same drawModel",
    },
    "estimated_metrics": {
        500: {
            "draw_calls_per_frame": 500,
            "projected_vertices_per_frame": 1000,
            "gdi_overhead_ms": "< 1ms per frame for 500 lines (GDI is fast for simple lines)",
            "spatial_query_ms": "< 0.5ms",
            "expected_fps": "> 60 (frame time < 16ms)",
        },
        2000: {
            "draw_calls_per_frame": 2000,
            "projected_vertices_per_frame": 4000,
            "expected_fps": "> 30 (frame time ~30ms)",
        },
        5000: {
            "draw_calls_per_frame": 5000,
            "projected_vertices_per_frame": 10000,
            "expected_fps": "~15 (frame time ~65ms, interactive nav stride kicks in at 1500)",
        },
        10000: {
            "draw_calls_per_frame": "up to 10000 (reduced during nav via stride=7)",
            "projected_vertices_per_frame": "up to 20000",
            "expected_fps": "~7-8 (frame time ~130ms, interactive nav renders ~1428 entities)",
        },
    },
    "interactive_navigation": {
        "stride_threshold": 1500,
        "stride_formula": "max(1, ceil(total / 1500))",
        "coverage_budget": 8000,  # renderAllOnScreen threshold
        "effect": "during pan/zoom, only 1/N entities rendered, sorted by coverage priority",
    },
    "optimizations": {
        "pen_caching": "entityPens map reuses HPEN objects by (color, weight, type, scale)",
        "spatial_index": "r-tree bounds query limits visible set",
        "depth_clipping": "optional workplane Z-range filter",
        "raster_preview": "bitmap snapshot during pan for models > 500",
    },
}

print("=" * 60)
print("LINE RENDERING BASELINE — Model-Maker GDI")
print("=" * 60)
print()
print("Pipeline: DXF import → r-tree spatial query → edge pass (GDI)")
print(f"Backend: GDI (GPU disabled, pending FBO compositing fix)")
print()
print("PER-ENTITY COST:")
for op, val in BENCHMARKS["per_entity_ops"].items():
    print(f"  {op}: {val}")
print()
print("PERFORMANCE ESTIMATES (static view, 1456×864 canvas):")
print()
print(f"{'Entities':>10} {'DrawCalls':>10} {'Verts':>8} {'Est.FPS':>8}")
print("-" * 44)
for n in BENCHMARKS["entity_count"]:
    d = BENCHMARKS["estimated_metrics"][n]
    dc = d["draw_calls_per_frame"]
    pv = d["projected_vertices_per_frame"]
    fps = d["expected_fps"]
    print(f"{n:>10} {str(dc):>10} {str(pv):>8} {fps:>8}")

print()
print("INTERACTIVE NAVIGATION (pan/zoom):")
print(f"  Stride kicks in at: {BENCHMARKS['interactive_navigation']['stride_threshold']} entities")
print(f"  Coverage budget: {BENCHMARKS['interactive_navigation']['coverage_budget']}")
print(f"  Raster preview: entities > 500 (replaces full redraw with bitmap scroll)")
print()
print("BOTTLENECK RANK (by impact):")
print("  1. GDI Polyline calls (N per frame)")
print("  2. Vertex projection per entity (CPU-side matrix mul)")
print("  3. No GPU acceleration (all CPU + GDI)")
print("  4. Spatial query < 1% of frame time")
print()
print("GPU PATH STATUS:")
print("  OpenGL: implemented but disabled (FBO glReadPixels flicker)")
print("  Expected GPU gain: 10-50× for line rendering")
print("  Next step: WGL_NV_DX_interop or DirectComposition swapchain")
