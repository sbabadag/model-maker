---
name: model-maker-build-verify
description: Build and verify the affected Model-Maker target.
version: 1.0.0
author: Selahattin Model-Maker Pack
license: MIT
platforms: [windows]
metadata:
  hermes:
    tags: [build, msbuild, cmake, visual-studio, tests]
    category: software-development
    requires_toolsets: [terminal]
---

# Model-Maker Build and Verification

## When to Use

Use after code changes or when diagnosing compilation, linking, or test failures.

## Build Detection

Inspect in this order:

1. `CMakePresets.json`
2. root `CMakeLists.txt`
3. `.sln`
4. `.vcxproj`
5. existing build scripts and project documentation

Do not invent a build command before detecting the project system.

## Procedure

1. Record the current directory and selected configuration.
2. Prefer existing presets and scripts.
3. Build the smallest affected target before the full solution.
4. Capture the first meaningful compiler or linker error with surrounding lines.
5. Fix one error class at a time.
6. Rebuild after each coherent fix.
7. Run focused tests, then a smoke test relevant to the user-visible behavior.

## Typical Commands

CMake preset:

```powershell
cmake --preset <preset>
cmake --build --preset <build-preset> --target <target>
ctest --preset <test-preset> --output-on-failure
```

Visual Studio solution:

```powershell
$msbuild = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" `
  -latest -products * -requires Microsoft.Component.MSBuild `
  -find MSBuild\**\Bin\MSBuild.exe |
  Select-Object -First 1

& $msbuild ".\ModelMaker.sln" /m /p:Configuration=Debug
```

Only use names confirmed in the repository.

## Performance Verification

For rendering changes, record at least:

- representative object count
- average and worst frame time
- draw-call count when available
- GPU/CPU bottleneck evidence
- selection latency for a representative scene

Do not claim a performance improvement without before/after measurements.

## Pitfalls

- `Clean` is not the default first step; it hides incremental-build problems and costs
  time.
- Linker errors often result from target membership or missing definitions, not header
  syntax.
- A successful compile does not prove correct object selection or rendering.

## Verification

Report the exact command, target, configuration, exit result, and test/smoke result.
