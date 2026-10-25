#!/usr/bin/env python3
"""Verify a Bridge V3 adapter package and print a truthful conversion summary.
Does NOT compare solids or prove Tekla / Model Maker parametric equivalence.
"""
import argparse
import hashlib
import json
from pathlib import Path


def verify(folder: Path) -> tuple[bool, list[str], dict]:
    errors = []
    def read(name):
        p = folder / name
        if not p.is_file():
            errors.append(name + " missing")
            return None
        try: return json.loads(p.read_text(encoding="utf-8"))
        except (ValueError, UnicodeError):
            errors.append(name + " invalid JSON")
            return None
    m, c, params, objects, diag, checksums = [read(n) for n in (
        "manifest.json", "component.json", "parameters.json", "objects.json", "diagnostics.json", "checksums.json")]
    if not all(x is not None for x in (m,c,params,objects,diag,checksums)):
        return False, errors, {}
    if not all(isinstance(x, dict) for x in (m,c,diag,checksums)) or not all(isinstance(x, list) for x in (params,objects)):
        errors.append("wrong JSON root type")
        return False, errors, {}
    if m.get("formatVersion") != "3.0": errors.append("unsupported formatVersion")
    if not isinstance(m.get("componentIdentifier"), str) or not m["componentIdentifier"]:
        errors.append("manifest componentIdentifier empty")
    if c.get("guid") != m.get("componentIdentifier"): errors.append("component GUID mismatch")
    if len(objects) != m.get("exportedObjectCount"): errors.append("object count mismatch")
    ids = [o.get("sourceGUID") for o in objects if isinstance(o, dict)]
    if len(ids) != len(set(ids)) or not all(ids): errors.append("object IDs duplicate or empty")
    if any(".." in Path(k).parts or Path(k).is_absolute() or "\\" in k for k in checksums):
        errors.append("unsafe checksum path")
    else:
        for rel, expected in checksums.items():
            fp = folder / rel
            if not fp.is_file() or hashlib.sha256(fp.read_bytes()).hexdigest() != expected:
                errors.append("checksum mismatch: " + rel)
    summary = {"component": c.get("name"), "number": c.get("number"),
               "objects": len(objects), "parameters": len(params),
               "warnings": len(diag.get("warnings", [])),
               "unsupportedIssues": len(diag.get("unsupportedFeatures", [])),
               "reconstructedSolidsVerified": False}
    return not errors, errors, summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("folder", type=Path)
    args = parser.parse_args()
    ok, errs, summary = verify(args.folder)
    print(json.dumps({"packageValid": ok, "errors": errs, "summary": summary}, ensure_ascii=False, indent=2))
    raise SystemExit(0 if ok else 1)


if __name__ == "__main__":
    main()
