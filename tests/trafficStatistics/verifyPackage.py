"""Inspect the staged IFW payload without executing any packaged binary.

Run from a vcvarsall amd64 environment. Pass the package data directory and
an output JSON path. PE dependencies resolve against the payload and System32;
API-set contracts are provided by Windows rather than standalone DLL files.
"""
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

payload = Path(sys.argv[1]).resolve()
output = Path(sys.argv[2]).resolve()
required = [
    "AmneziaVPN.exe", "AmneziaVPN-service.exe", "vc_redist.x64.exe",
    "dxcompiler.dll", "dxil.dll", "d3dcompiler_47.dll", "opengl32sw.dll",
    "vcruntime140.dll", "vcruntime140_1.dll", "msvcp140.dll",
    "Qt6Core.dll", "Qt6Concurrent.dll", "Qt6Quick.dll", "Qt6Qml.dll",
    "Qt6Network.dll", "Qt6RemoteObjects.dll", "Qt6QuickControls2.dll",
    "libcrypto-3-x64.dll", "libssl-3-x64.dll", "openvpn.exe", "tun2socks.exe",
    "amnezia_xray.dll", "tunnel.dll", "wintun.dll", "mullvad-split-tunnel.sys",
    "plugins/platforms/qwindows.dll", "qml/QtQuick/qmldir",
    "qml/QtQuick/Controls/qmldir", "translations/qt_ru.qm",
]
missing = [name for name in required if not (payload / name).is_file()]
files = sorted(p for p in payload.rglob("*") if p.is_file())

def sha256(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()

root_names = {p.name.lower() for p in payload.iterdir() if p.is_file()}
system = Path(os.environ["WINDIR"]) / "System32"
unresolved = []
imports = {}
for path in files:
    if path.suffix.lower() not in (".exe", ".dll"):
        continue
    result = subprocess.run(["dumpbin", "/nologo", "/dependents", str(path)],
                            capture_output=True, text=True, errors="replace", check=True)
    names = re.findall(r"^\s+([\w.+-]+\.dll)\s*$", result.stdout, re.MULTILINE | re.IGNORECASE)
    imports[str(path.relative_to(payload))] = sorted(set(names))
    local_names = {p.name.lower() for p in path.parent.iterdir() if p.is_file()}
    for name in names:
        if name.lower().startswith(("api-ms-", "ext-ms-")):
            continue
        if name.lower() not in root_names | local_names and not (system / name).is_file():
            unresolved.append({"file": str(path.relative_to(payload)), "dependency": name})
manifest = {
    "payload": str(payload), "required_missing": missing, "unresolved_imports": unresolved,
    "imports": imports,
    "files": [{"path": str(p.relative_to(payload)), "bytes": p.stat().st_size,
               "sha256": sha256(p)} for p in files],
}
output.write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding="utf-8")
print(f"Checked {len(files)} payload files and imports of {len(imports)} PE binaries")
print(f"Required files missing: {missing}")
print(f"Unresolved imports: {unresolved}")
sys.exit(bool(missing or unresolved))
