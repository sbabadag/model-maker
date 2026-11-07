# Model-Maker Performance Bottleneck Analysis

## Overview
This skill documents performance bottlenecks in the Model-Maker CAD application when handling large numbers of entities, particularly focusing on render system scalability.

## Key Findings

### Interactive Navigation System
The code includes built-in logic to maintain interactivity with large drawings:

1. **Model Stride Logic**: 
   ```cpp
   constexpr std::size_t interactiveModelBudget = 1'500;
   interactiveModelStride = std::max<std::size_t>(1,
       (document.models().size() + interactiveModelBudget - 1) / interactiveModelBudget);
   ```

2. **Limited Rendering Strategy**: Only renders every Nth model when `interactiveNavigation` is enabled to prevent performance degradation.

### Spatial Indexing System
The Document class uses a hierarchical spatial indexing system:
- `queryBounds()` and `query2D()` functions for efficient clipping
- Spatial tree structure (`SpatialNode`) for entity visibility detection

### Render Resource Management  
- Frame performance tracking with metrics like draw calls, projected vertices
- Dynamic backbuffer management
- Vector reallocation patterns in rendering pipeline

## Bottlenecks Identified

### 1. Entity Count Thresholds
When document contains >1500 models:
- Interactive navigation kicks in automatically  
- Only every `interactiveModelStride` model gets rendered
- Performance may still degrade with high entity counts and large draw sets

### 2. Memory Reallocation
- `projectedChain` vector grows during rendering 
- Potential for memory reallocation spikes during large render operations
- Large vector allocations might impact rendering frame rates

### 3. Backbuffer Management
- Dynamic backbuffer allocation based on viewport size
- May cause performance issues with massive drawings

## Verification Strategy

To quantify bottlenecks:
1. **Performance Metrics Collection**: Capture performance data from `FramePerformanceTracker` under various entity loads
2. **Entity Count Threshold Testing**: Test how performance degrades beyond 10K+ entities
3. **Memory Allocation Analysis**: Evaluate vector growth patterns with large model sets

## Tools Required
- Performance monitoring through existing `performance.hpp` system
- Entity counting tools to simulate large drawing loads  
- Memory profiling to capture reallocation behavior

## References
- File: `src/renderer.cpp`
- File: `src/application.cpp` 
- File: `include/model_maker/performance.hpp`
- File: `include/model_maker/document.hpp`