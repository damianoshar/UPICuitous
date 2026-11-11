"""Download JUCE as a zip (no git required) into ./JUCE.

The archive is verified against a pinned SHA-256 before it is extracted, so a
tampered download (or a hijacked mirror) cannot end up in the build.

v1.30 (S-04/S-05): the version moved to 8.0.16-security (the JUCE release
that carries the malformed-input fixes) and the hash check was added.
"""
import hashlib
import pathlib
import shutil
import sys
import urllib.request
import zipfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
DEST = ROOT / "JUCE"
VERSION = "8.0.16-security"
URL = f"https://github.com/juce-framework/JUCE/archive/refs/tags/{VERSION}.zip"

# The SHA-256 of the archive above. Update it together with VERSION.
SHA256 = "7f75cee28ea4dd216fc49ba6fb57f4baa14729ef97529040d3a99dd04910f166"


def sha256_of(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def main():
    if (DEST / "CMakeLists.txt").exists():
        print(f"JUCE already present at {DEST}, skipping.")
        return 0

    tmp = ROOT / f"juce-{VERSION}.zip"
    print(f"Downloading {URL} ...")
    print(f"  -> {tmp} (~26MB, be patient)")
    urllib.request.urlretrieve(URL, tmp)

    print("Verifying the SHA-256 ...")
    digest = sha256_of(tmp)
    if digest != SHA256:
        tmp.unlink(missing_ok=True)
        print(f"FAILED: the archive's SHA-256 is {digest}, expected {SHA256}")
        print("The download was discarded - nothing was extracted.")
        return 1
    print(f"  ok: {digest}")

    print("Extracting ...")
    with zipfile.ZipFile(tmp) as z:
        z.extractall(ROOT / "_juce_tmp")
    src = ROOT / "_juce_tmp" / f"JUCE-{VERSION}"
    if DEST.exists():
        shutil.rmtree(DEST)
    shutil.move(str(src), str(DEST))
    shutil.rmtree(ROOT / "_juce_tmp")
    tmp.unlink(missing_ok=True)
    print(f"Done. JUCE {VERSION} at {DEST}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
