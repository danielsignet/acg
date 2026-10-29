"""Fetch the prototype's six 1K CC0 texture files. No runtime network access."""
import hashlib
import json
from pathlib import Path
import urllib.request

HERE = Path(__file__).resolve().parent
ASSETS = ("marble_01", "beige_wall_001", "wood_table_001")


def get(url):
    if not url.startswith(("https://api.polyhaven.com/", "https://dl.polyhaven.org/")):
        raise ValueError("Unexpected asset host: " + url)
    request = urllib.request.Request(url, headers={"User-Agent": "ACG-Pantheon-Experiment/1.0"})
    with urllib.request.urlopen(request, timeout=45) as response:
        return response.read()


def main():
    target = HERE / "textures"
    target.mkdir(exist_ok=True)
    catalog = json.loads(get("https://api.polyhaven.com/assets?t=textures"))
    manifest = []
    for asset in ASSETS:
        files = json.loads(get("https://api.polyhaven.com/files/" + asset))
        for role, keys in (("albedo", ("diff", "Diffuse", "diffuse")), ("normal", ("nor_gl",))):
            channel = next((files[k] for k in keys if k in files), None)
            if channel is None:
                raise ValueError(f"No {role} channel for {asset}: {list(files)}")
            info = channel["1k"]["jpg"]
            path = target / f"{asset}-{role}.jpg"
            data = path.read_bytes() if path.exists() else get(info["url"])
            if hashlib.md5(data).hexdigest() != info["md5"]:
                raise ValueError("Download checksum mismatch: " + str(path))
            path.write_bytes(data)
            manifest.append({"asset": asset, "role": role, "file": path.relative_to(HERE).as_posix(),
                             "source": "https://polyhaven.com/a/" + asset, "url": info["url"],
                             "authors": catalog[asset].get("authors", {}), "license": "CC0-1.0",
                             "license_url": "https://polyhaven.com/license", "bytes": len(data),
                             "sha256": hashlib.sha256(data).hexdigest()})
            print(f"{path.name}: {len(data):,} bytes")
    (HERE / "materials.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
