"""Create an allowlisted portable ZIP; never collect local settings or logs."""
import argparse
import hashlib
import json
from pathlib import Path
import zipfile

ROOT = Path(__file__).resolve().parents[1]
TOP = {"CstChineseLauncher.exe", "start-zh.cmd", "README.md", "TEST_RESULTS.md", ".gitignore", "CODEX_PROGRESS.md"}
PLUGIN = "CSTRibbonZh/plugins/generic/CstRibbonZh.dll"


def files():
    chosen = []
    for path in ROOT.rglob("*"):
        if not path.is_file():
            continue
        name = path.relative_to(ROOT).as_posix()
        if name in TOP or name == PLUGIN:
            chosen.append(path)
        elif name.startswith(("src/", "tools/", "tests/")):
            if any(part in {"plugins", "__pycache__", ".build"} for part in path.relative_to(ROOT).parts):
                continue
            if path.name == "user.tsv":
                continue
            if path.suffix in {".cs", ".cpp", ".h", ".json", ".tsv", ".py", ".ps1", ".js", ".html"}:
                chosen.append(path)
    names = {p.relative_to(ROOT).as_posix() for p in chosen}
    required = TOP | {PLUGIN, "src/Launcher.cs", "src/CSTRibbonZh/build.ps1", "src/CSTRibbonZh/smoke.cpp"}
    if required - names:
        raise RuntimeError("Missing package files: " + str(sorted(required - names)))
    return sorted(chosen)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    selected = files()
    manifest = {p.relative_to(ROOT).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest() for p in selected}
    metadata = json.dumps({"format": 1, "sha256": manifest}, ensure_ascii=False, indent=2).encode("utf-8")
    (ROOT / "MANIFEST.json").write_bytes(metadata)
    with zipfile.ZipFile(args.output, "w", zipfile.ZIP_DEFLATED) as archive:
        for path in selected:
            archive.write(path, "CST-Chinese-Portable/" + path.relative_to(ROOT).as_posix())
        archive.writestr("CST-Chinese-Portable/MANIFEST.json", metadata)
    with zipfile.ZipFile(args.output) as archive:
        assert archive.testzip() is None
        for name, sha in manifest.items():
            assert hashlib.sha256(archive.read("CST-Chinese-Portable/" + name)).hexdigest() == sha
    print(f"PASS {len(manifest)} files: CRC and SHA-256 verified")
    print("ZIP SHA256 " + hashlib.sha256(args.output.read_bytes()).hexdigest())


if __name__ == "__main__":
    main()
