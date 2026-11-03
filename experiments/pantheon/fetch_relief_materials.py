"""Fetch the two CC0 scanned material sets used by the parallax demonstrations."""
import hashlib
import json
from pathlib import Path
import urllib.request

HERE = Path(__file__).resolve().parent


def get(url):
    if not url.startswith(("https://api.polyhaven.com/", "https://dl.polyhaven.org/")):
        raise ValueError("Unexpected material host: " + url)
    request = urllib.request.Request(url, headers={"User-Agent": "ACG-Pantheon-Student-Project/1.0"})
    with urllib.request.urlopen(request, timeout=45) as response:
        return response.read()


def main():
    target = HERE / "textures"
    target.mkdir(exist_ok=True)
    manifest = []
    for asset in ("bark_brown_01", "red_brick"):
        files = json.loads(get("https://api.polyhaven.com/files/" + asset))
        for role, keys, fmt in (("albedo", ("diff", "Diffuse"), "jpg"),
                                ("normal", ("nor_gl",), "png"),
                                ("height", ("disp", "Displacement"), "png")):
            channel = next(files[key] for key in keys if key in files)
            info = channel["2k"][fmt]
            path = target / f"{asset}-{role}.{fmt}"
            data = path.read_bytes() if path.exists() else get(info["url"])
            if hashlib.md5(data).hexdigest() != info["md5"]:
                raise ValueError("Download checksum mismatch: " + str(path))
            path.write_bytes(data)
            manifest.append({"asset": asset, "role": role,
                             "file": path.relative_to(HERE).as_posix(),
                             "source": "https://polyhaven.com/a/" + asset,
                             "url": info["url"], "license": "CC0-1.0",
                             "license_url": "https://polyhaven.com/license",
                             "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()})
            print(f"{path.name}: {len(data):,} bytes")
    (HERE / "relief-materials.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
