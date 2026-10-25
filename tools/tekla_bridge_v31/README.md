# Tekla Bridge V3.1 — Stage 1 (legacy-to-V3 adapter)

This is an isolated **prototype**. It does not modify Model Maker's C++ importer, its Document/EntityId/OCCT store, or the active Windows PERF-003A branch.

It converts legacy Tekla Open API JSON probes into directories compatible with Model Maker Bridge V3 (`manifest.formatVersion` remains `3.0`). It does **not** claim exact B-Rep equivalence or produce an editable Tekla component automatically.

## Windows PowerShell quick start

From an existing Model Maker checkout (leave the active PERF-003A worktree untouched):

```powershell
# Run this once, from your existing checkout:
git fetch origin tmp/bridge-v3.1-stage1-20261025
git worktree add --detach C:\work\model-maker-bridge-v31-test origin/tmp/bridge-v3.1-stage1-20261025

cd C:\work\model-maker-bridge-v31-test\tools\tekla_bridge_v31
py -3 -m unittest discover -s tests -v
py -3 bridge31_adapter.py fixtures\end_plate_144.json --out output\end_plate_144-v3
py -3 verify_bridge31.py output\end_plate_144-v3
```

The public GitHub repo contains **only synthetic test data**. Real 146 / Haunch-40 customer geometry probes are intentionally excluded. Two corresponding tests will be skipped; to run them locally, copy `component-5245-v2.json` and `component-990503.json` from the private Stage 1 archive to `tools\tekla_bridge_v31\fixtures\` (never commit them to the public repo).

To try a real component from a local JSON probe (outside source control):

```powershell
py -3 bridge31_adapter.py C:\path\to\component-5245-v2.json --out output\real-146-v3
py -3 verify_bridge31.py output\real-146-v3
```

**Preconditions:** Python 3.10+; no third-party Python packages needed. Output directories must not exist. Do not merge this branch into the production branch before native C++ import and Windows tests.

## Limitations

- Format V3 `3.0` compatibility, not a new `formatVersion 3.1`.
- Probe ID values are not stable Tekla GUIDs. Don't auto-bind them to `EntityId`.
- Missing cuts, bolt axes, hole details, weld geometry, plate B-Rep, or attributes remain unresolved; never infer geometric equivalence from a parsing success.
- Stage 2: native C# exporter targeting Tekla 2025 SP6 and Bridge V3, followed by Model Maker native importer regression tests.