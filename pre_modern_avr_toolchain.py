Import("env")

from pathlib import Path
import io
import os
import shutil
import urllib.request
import zipfile

project_dir = Path(env.subst("$PROJECT_DIR"))
cache_root = project_dir / ".pio"
toolchain_dir = cache_root / "avr-gcc-12.1.0"
stdlib_dir = cache_root / "avr-libstdcpp"

toolchain_url = (
    "https://github.com/ZakKemble/avr-gcc-build/releases/download/"
    "v12.1.0-1/avr-gcc-12.1.0-x64-windows.zip"
)
stdlib_url = "https://codeload.github.com/modm-io/avr-libstdcpp/zip/refs/heads/master"

def download_zip(url, destination, expected_child):
    if (destination / expected_child).exists():
        return

    print("Downloading %s ..." % url)
    if destination.exists():
        shutil.rmtree(destination)

    with urllib.request.urlopen(url, timeout=120) as response:
        data = response.read()

    temp_root = cache_root / "_tool_extract"
    if temp_root.exists():
        shutil.rmtree(temp_root)
    temp_root.mkdir(parents=True)

    with zipfile.ZipFile(io.BytesIO(data)) as zf:
        zf.extractall(temp_root)

    children = [p for p in temp_root.iterdir()]
    if len(children) == 1 and children[0].is_dir():
        shutil.move(str(children[0]), str(destination))
    else:
        destination.mkdir(parents=True, exist_ok=True)
        for child in children:
            shutil.move(str(child), str(destination / child.name))

    shutil.rmtree(temp_root)

download_zip(toolchain_url, toolchain_dir, Path("bin") / "avr-g++.exe")
download_zip(stdlib_url, stdlib_dir, Path("include") / "array")

bin_dir = toolchain_dir / "bin"

required = {
    "CC": bin_dir / "avr-gcc.exe",
    "CXX": bin_dir / "avr-g++.exe",
    "AR": bin_dir / "avr-ar.exe",
    "RANLIB": bin_dir / "avr-ranlib.exe",
    "OBJCOPY": bin_dir / "avr-objcopy.exe",
    "SIZETOOL": bin_dir / "avr-size.exe",
}

missing = [str(path) for path in required.values() if not path.exists()]
if missing:
    raise RuntimeError("Modern AVR toolchain incomplete: " + ", ".join(missing))

env.Replace(
    CC=str(required["CC"]),
    CXX=str(required["CXX"]),
    AR=str(required["AR"]),
    RANLIB=str(required["RANLIB"]),
    OBJCOPY=str(required["OBJCOPY"]),
    SIZETOOL=str(required["SIZETOOL"]),
)

env["ENV"]["PATH"] = str(bin_dir) + os.pathsep + env["ENV"].get("PATH", "")

print("Using modern AVR-GCC toolchain: %s" % toolchain_dir)
print("Using avr-libstdcpp headers: %s" % (stdlib_dir / "include"))
