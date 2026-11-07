---
name: model-maker-opengl-cad-performance
description: Optimize Model-Maker CAD rendering and selection.
version: 1.0.0
author: Selahattin Model-Maker Pack
license: MIT
platforms: [windows]
metadata:
  hermes:
    tags: [opengl, cad, rendering, picking, performance]
    category: software-development
---

# Model-Maker OpenGL CAD Performance

## When to Use

Use for frame-rate, flicker, scene navigation, object manipulation, large-model
rendering, selection, picking, or viewport refresh work.

## Diagnostic Order

1. Reproduce with a representative model and fixed camera path.
2. Measure CPU frame time, GPU frame time, draw calls, submitted primitives, buffer
   uploads, allocations, and synchronization.
3. Determine whether the primary limit is:
   - scene traversal
   - state changes and draw calls
   - geometry upload
   - fill rate
   - shader cost
   - synchronization/stalls
   - selection/picking
   - UI/render invalidation
4. Change one bottleneck at a time and compare before/after.

## Preferred Architecture

Use existing project conventions where possible, but evaluate these patterns:

- retained-mode scene representation
- stable object IDs separated from GPU offsets
- CPU-side dirty flags and revision counters
- GPU buffers updated by changed ranges, not fully rebuilt every frame
- batching by pipeline/material/primitive type
- frustum culling before submission
- spatial index for broad-phase queries
- separate visible, highlighted, preview, and selected render passes
- command-based undo/redo outside rendering
- frame-local transient allocator for temporary render data
- asynchronous CPU preparation where safe
- explicit render-thread ownership of OpenGL calls

## Spatial Index Choice

- Uniform grid: predictable object sizes and evenly distributed scenes.
- Quadtree: primarily planar CAD scenes with uneven density.
- Octree: genuinely volumetric 3D scenes.
- BVH/AABB tree: varied object sizes and ray or frustum queries.
- R-tree: 2D spatial database-style extents and frequent window queries.

Do not select an index from theory alone. Consider insertion/update cost because CAD
objects are edited interactively.

## Selection Strategy

Prefer a layered approach:

1. Screen-space coarse rejection.
2. Spatial-index candidate query.
3. CPU ray/AABB or geometric narrow phase for precision.
4. Optional GPU ID-buffer picking for dense visible scenes.
5. Stable object ID returned to the model layer.

Avoid synchronous full-resolution GPU readback on every mouse move. Use reduced
frequency, a small picking region, persistent/staged readback, or CPU broad phase.

## Flicker Prevention

- redraw from a complete retained scene
- use double buffering
- avoid clearing/redrawing from competing UI paths
- avoid deleting and recreating buffers during interactive motion
- keep preview geometry in a dedicated transient layer
- update transforms independently from immutable geometry where possible
- invalidate only what changed, while still presenting complete frames

## Red Flags

- one draw call per primitive for large scenes
- rebuilding all vertex buffers after one object edit
- `glFinish` or frequent blocking queries in the frame loop
- per-frame heap allocation per object
- CPU/GPU synchronization for hover selection
- storing UI selection state inside low-level GPU objects
- optimizing without measurements

## Verification

Provide before/after measurements and identify the dominant bottleneck that changed.
For object manipulation, verify smooth drag, correct final geometry, selection
correctness, undo/redo, and no stale GPU data.
