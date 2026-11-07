---
name: model-maker-performance-measurement
description: Procedure for measuring performance bottlenecks in Model-Maker CAD application with large entity counts.
version: 1.0.0
author: Hermes Agent
license: MIT
metadata:
  hermes:
    tags: [cpp, opengl, cad, performance, testing]
    category: software-development
    requires_toolsets: [terminal]
---

# Model-Maker Performance Measurement

## Overview
This skill outlines the procedure for measuring performance bottlenecks when rendering large numbers of entities in Model-MMaker.

## Procedure

### Initial Setup
1. **Environment**: Ensure model-maker-cpp-change-loop is set up
2. **Project Configuration**: Use model-maker-build-verify for compilation verification 
3. **Test Environment**: Use model-maker-opengl-cad-performance for performance testing

### Performance Measurement Process

1. **Baseline Collection**
   - Set up test environment with known entity counts (1K, 10K, 100K entities)
   - Run performance measurements using existing `FramePerformanceTracker`
   - Collect metrics: FPS, CPU frame time, draw calls, projected vertices

2. **Entity Scale Testing**
   - Create test drawings with increasing numbers of entities
   - Monitor performance drop-off points at various thresholds:
     * 1K entities (baseline)
     * 10K entities 
     * 100K entities
     * 1M entities (if system supports)
   - Track metrics including: 
     * Frame rate stability 
     * Memory allocation patterns
     * Vector reallocations
     * Backbuffer management

3. **Interactive Navigation Thresholds**
   - Test scenarios where `interactiveNavigation` triggers automatically
   - Measure performance impact of model stride logic
   - Observe how entity selection affects rendering performance

## Metrics to Monitor

### Core Performance Indicators:
- Frames Per Second (FPS)
- CPU Frame Time (ms per frame)
- Draw Calls Count
- Projected Vertices Count  
- Memory Reallocations
- Buffer Growths
- Spatial Query Times

### Entity-Specific Metrics:
- Total Entities vs Visible Entities
- Rendered vs Culled Entities
- Interactive Model Stride Value

## Testing Scenarios

1. **Idle Navigation**: Move around large drawings without interaction
2. **Zoom Operations**: Pan and zoom through large models 
3. **Entity Selection**: Select and manipulate large groups of entities
4. **Dynamic Input**: Use dynamic input features with many entities
5. **Result Visualization**: Show OpenSees structural analysis results

## Tools Required

1. Performance monitoring built into `performance.hpp` system
2. Visual indicators for entity counts
3. Frame rate measurement capabilities
4. Memory usage tracking tools 
5. Entity count simulation capabilities

## Data Analysis

- Create performance curves showing FPS vs entity count
- Determine exact thresholds where performance drops significantly  
- Correlate memory reallocations with large entity counts
- Document how `interactiveModelStride` impacts rendering at various scales

## Verification

After testing, validate:
1. Performance degradation matches expected patterns
2. Thresholds align with interactive navigation logic 
3. Memory allocation issues are identified and documented