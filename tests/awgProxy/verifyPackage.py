"""Check the staged installer payload without running packaged applications."""
from pathlib import Path
import runpy
import sys

payload = Path(sys.argv[1]).resolve()
required = ("amnezia-awg-proxy.exe", "licenses/awg-proxy/THIRD_PARTY_LICENSES.txt")
missing = [name for name in required if not (payload / name).is_file()]
if missing:
    raise SystemExit(f"AWG proxy payload files missing: {missing}")

# The existing verifier checks Qt/runtime files, resolves PE imports, and writes
# SHA-256 hashes for every payload file, including the helper and its licenses.
runpy.run_path(str(Path(__file__).resolve().parents[1] / "trafficStatistics/verifyPackage.py"),
              run_name="__main__")
