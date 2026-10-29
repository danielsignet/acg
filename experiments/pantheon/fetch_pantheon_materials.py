"""Fetch only the CC0 surface maps used by the Pantheon architecture.

Run explicitly when rebuilding the local asset bundle; generation and viewing
remain offline. Original files are retained without image processing.
"""
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path
import urllib.request

HERE = Path(__file__).resolve().parent
SETS = (
    ("beige_wall_001", "pantheon-plaster", {"Dimitrios Savva": "Photography", "Rico Cilliers": "Processing"}),
    ("marble_01", "pantheon-marble", {"Rob Tuytel": "All"}),
    ("grey_cartago_03", "pantheon-stone", {"Lucas Sánchez Ros": "All"}),
)
MAPS = (("albedo", "diff", "2k", "jpg"), ("normal", "nor_gl", "2k", "jpg"),
        ("height", "disp", "1k", "png"))


def fetch(spec):
    asset, prefix, authors, role, channel, resolution, extension = spec
    relative = f"textures/{prefix}-{role}.{extension}"
    path = HERE / relative
    url = (f"https://dl.polyhaven.org/file/ph-assets/Textures/{extension}/{resolution}/"
           f"{asset}/{asset}_{channel}_{resolution}.{extension}")
    if not path.exists():
        request = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
        with urllib.request.urlopen(request, timeout=30) as response:
            data = response.read()
        path.write_bytes(data)
    data = path.read_bytes()
    return dict(asset=asset, role=role, file=relative, source=f"https://polyhaven.com/a/{asset}",
                url=url, authors=authors, license="CC0-1.0", license_url="https://polyhaven.com/license",
                bytes=len(data), sha256=hashlib.sha256(data).hexdigest(), resolution=resolution)


if __name__ == "__main__":
    specs = [(asset, prefix, authors, *maps) for asset, prefix, authors in SETS for maps in MAPS]
    with ThreadPoolExecutor(max_workers=4) as pool:
        records = list(pool.map(fetch, specs))
    manifest = HERE / "materials.json"
    previous = json.loads(manifest.read_text(encoding="utf-8"))
    replacements = {record["file"] for record in records}
    previous = [record for record in previous if record["file"] not in replacements]
    manifest.write_text(json.dumps(previous + records, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"Verified {len(records)} Pantheon maps; {sum(r['bytes'] for r in records):,} bytes")
