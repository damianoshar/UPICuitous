"""Download JUCE 8.0.6 as zip (no git required) into ./JUCE."""
import urllib.request, zipfile, pathlib, shutil, sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
DEST = ROOT / "JUCE"
VERSION = "8.0.6"
URL = f"https://github.com/juce-framework/JUCE/archive/refs/tags/{VERSION}.zip"

def main():
    if (DEST / "CMakeLists.txt").exists():
        print(f"JUCE already present at {DEST}, skipping.")
        return
    tmp = ROOT / f"juce-{VERSION}.zip"
    print(f"Downloading {URL} ...")
    print(f"  -> {tmp} (~50MB, be patient)")
    urllib.request.urlretrieve(URL, tmp)
    print("Extracting ...")
    with zipfile.ZipFile(tmp) as z:
        z.extractall(ROOT / "_juce_tmp")
    src = ROOT / "_juce_tmp" / f"JUCE-{VERSION}"
    if DEST.exists():
        shutil.rmtree(DEST)
    shutil.move(str(src), str(DEST))
    shutil.rmtree(ROOT / "_juce_tmp")
    tmp.unlink(missing_ok=True)
    print(f"Done. JUCE at {DEST}")
    print("Now run: cmake -S . -B build -G \"MinGW Makefiles\" -DCMAKE_BUILD_TYPE=Release")

if __name__ == "__main__":
    sys.exit(main())
