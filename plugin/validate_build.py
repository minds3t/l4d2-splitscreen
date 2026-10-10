"""Validate the x86 plugin build and write its current static artifact manifest.

Run after successful CTest. This records PE structure, source hashes and the
actual CTest log/fixture binaries; it makes no game-runtime certification.
"""
from argparse import ArgumentParser
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re


FIXTURES = ("guest_abi", "input_abi", "safearea_abi")


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def utc(timestamp):
    return datetime.fromtimestamp(timestamp, timezone.utc).isoformat()


def validate_ctest(log_path, artifact, configuration_dir):
    """Require all completed passes from this configuration after its build."""
    raw = log_path.read_bytes()
    text = raw.decode("utf-8", errors="replace").replace("\r\n", "\n")  # Localized timezone may use ACP.
    if not re.search(r"^End testing:", text, re.M):
        raise ValueError("CTest log is incomplete")
    chunks = re.split(r"^\d+/\d+ Testing: ([^\r\n]+)\r?$", text, flags=re.M)
    names = chunks[1::2]
    if sorted(names) != sorted(FIXTURES):
        raise ValueError("CTest log must contain exactly: " + ", ".join(FIXTURES))
    passed = {}
    for name, body in zip(names, chunks[2::2]):
        if not re.search(r"^Test Passed\.$", body, re.M) or re.search(r"^Test Failed", body, re.M):
            raise ValueError(f"CTest fixture did not pass: {name}")
        command = re.search(r'^Command: "([^"]+)"\s*$', body, re.M)
        expected = configuration_dir / (name + "_test.exe")
        if command is None or Path(command[1]).resolve() != expected.resolve():
            raise ValueError(f"CTest fixture came from a different configuration: {name}")
        if not expected.is_file():
            raise ValueError(f"CTest fixture executable is missing: {expected}")
        if log_path.stat().st_mtime_ns < expected.stat().st_mtime_ns:
            raise ValueError(f"CTest log predates fixture executable: {name}")
        passed[name] = {"status": "passed", "executable": str(expected.resolve()),
                        "sha256": sha256(expected)}
    if log_path.stat().st_mtime_ns < artifact.stat().st_mtime_ns:
        raise ValueError("CTest log predates the DLL; run CTest after building")
    return {"log": str(log_path.resolve()), "log_sha256": hashlib.sha256(raw).hexdigest(),
            "log_modified_utc": utc(log_path.stat().st_mtime), "tests": passed}


def source_files(plugin_dir):
    """Project code, fixture code, MinHook code and build configuration."""
    files = set()
    for relative in ("src", "include", "tests", "vendor/minhook"):
        for path in (plugin_dir / relative).rglob("*"):
            if path.is_file() and path.suffix.lower() in (".cpp", ".c", ".h"):
                files.add(path)
    files.update((plugin_dir / name) for name in ("CMakeLists.txt", "exports.def"))
    return sorted(files)


def validate(plugin_dir, configuration):
    import pefile

    plugin_dir = plugin_dir.resolve()
    output = plugin_dir / "build" / configuration
    artifact = output / "splitscreen_support.dll"
    if not artifact.is_file():
        raise ValueError(f"DLL is missing: {artifact}")
    sources = source_files(plugin_dir)
    # A manually invoked validator must not silently certify newer source against
    # yesterday's DLL. Build scripts themselves are recorded separately below.
    newer = [str(p.relative_to(plugin_dir)) for p in sources
             if p.stat().st_mtime_ns > artifact.stat().st_mtime_ns]
    if newer:
        raise ValueError("Source is newer than the DLL; rebuild first: " + ", ".join(newer))
    source_hashes = {p.relative_to(plugin_dir).as_posix(): sha256(p) for p in sources}
    before = sha256(artifact)
    with pefile.PE(str(artifact)) as pe:
        if pe.FILE_HEADER.Machine != 0x14C or pe.OPTIONAL_HEADER.Magic != 0x10B:
            raise ValueError("The plugin must be a Windows x86 PE32 DLL")
        if not pe.FILE_HEADER.Characteristics & 0x2000:
            raise ValueError("The artifact does not have the DLL characteristic")
        exports = sorted(s.name.decode("ascii") for s in pe.DIRECTORY_ENTRY_EXPORT.symbols if s.name)
        if "CreateInterface" not in exports:
            raise ValueError("The plugin is missing the undecorated CreateInterface export")
        imports = sorted(entry.dll.decode("ascii") for entry in pe.DIRECTORY_ENTRY_IMPORT)
    tests = validate_ctest(plugin_dir / "build/Testing/Temporary/LastTest.log", artifact, output)
    if before != sha256(artifact) or any(source_hashes[p.relative_to(plugin_dir).as_posix()] != sha256(p)
                                         for p in sources):
        raise ValueError("Build/source changed during validation; retry after the build completes")
    workspace = plugin_dir.parent
    manifest = {
        "scope": "Static artifact/source/CTest validation only; no game-runtime certification.",
        "validated_at_utc": datetime.now(timezone.utc).isoformat(),
        "configuration": configuration,
        "artifact": str(artifact.relative_to(workspace)),
        "sha256": before,
        "bytes": artifact.stat().st_size,
        "machine": "0x14c",
        "pe_format": "PE32",
        "exports": exports,
        "imports": imports,
        "source_sha256": source_hashes,
        "validation_tools_sha256": {
            "plugin/build.ps1": sha256(plugin_dir / "build.ps1"),
            "plugin/validate_build.py": sha256(Path(__file__).resolve()),
        },
        "ctest": tests,
        "checks": {"source_not_newer_than_dll": True, "tests_not_older_than_dll_or_fixtures": True,
                   "all_required_ctests_passed": True, "dll_and_source_stable_during_validation": True},
    }
    destination = output / "validation.json"
    temporary = output / "validation.json.tmp"
    temporary.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    os.replace(temporary, destination)
    return destination, manifest


def main():
    parser = ArgumentParser(description=__doc__)
    parser.add_argument("--plugin-dir", type=Path, default=Path(__file__).resolve().parent)
    parser.add_argument("--configuration", choices=("Release", "Debug"), default="Release")
    args = parser.parse_args()
    try:
        path, manifest = validate(args.plugin_dir, args.configuration)
    except (ValueError, OSError) as error:
        parser.exit(1, f"Build validation failed: {error}\n")
    print(f"Validated {manifest['configuration']} x86 DLL: {manifest['sha256']}")
    print(f"All {len(FIXTURES)} CTests passed; static manifest: {path}")


if __name__ == "__main__":
    main()
