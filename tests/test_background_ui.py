"""Exercise the default isolated UI path, real GL captures, and timeout cleanup."""
import ctypes
from ctypes import wintypes
import json
from pathlib import Path
import re
import subprocess
import sys

from PIL import Image, ImageStat

exe = Path(sys.argv[1]).resolve()
output = Path(sys.argv[2]).resolve()
root = Path(__file__).resolve().parents[1]
output.mkdir(parents=True, exist_ok=True)

def run(arguments, log):
    result = subprocess.run([str(exe), *arguments], cwd=root, capture_output=True, timeout=120)
    text = (result.stdout + result.stderr).decode("utf-8", errors="replace")
    (output / log).write_text(text, encoding="utf-8")
    return result.returncode, text

code, log = run([
    "--scene", str(root / "resources/scenes/lantern.scene.json"),
    "--capture-ui", str(output / "capture"), "--capture-pages", "scene,material",
    "--capture-raster", "--capture-warmup", "0",
    "--capture-width", "1366", "--capture-height", "768",
], "capture.log")
assert code == 0, log[-6000:]
assert "isolated desktop verified" in log
audit = re.search(r"inputDesktopWindows=0 privateWindowsPeak=(\d+) checks=(\d+)", log)
assert audit and int(audit[1]) > 0 and int(audit[2]) > 0, log[-2000:]
report = json.loads((output / "capture/capture-report.json").read_text(encoding="utf-8"))
assert report["passed"] and report["background"] and len(report["captures"]) == 2
for page in report["captures"]:
    assert page["viewportCaptured"] and page["raster"], page
    with Image.open(page["viewportFile"]) as frame:
        assert frame.width >= 160 and frame.height >= 120
        assert max(ImageStat.Stat(frame.convert("RGB")).stddev) > 2, "Empty/constant GL capture"

# An indefinitely warming capture must be killed as a process tree, not left hidden and running.
code, log = run([
    "--capture-ui", str(output / "timeout"), "--capture-warmup", "60000",
    "--background-timeout-ms", "2000",
], "timeout.log")
assert code == 124 and "timed out" in log, log[-2000:]
child = re.search(r"isolated desktop verified, pid=(\d+)", log)
assert child, log
kernel = ctypes.WinDLL("kernel32", use_last_error=True)
kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
kernel.OpenProcess.restype = wintypes.HANDLE
kernel.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
kernel.CloseHandle.argtypes = [wintypes.HANDLE]
handle = kernel.OpenProcess(0x00100000, False, int(child[1]))
if handle:
    try:
        assert kernel.WaitForSingleObject(handle, 5000) == 0, "Timed-out child survived its supervisor"
    finally:
        kernel.CloseHandle(handle)
else:
    assert ctypes.get_last_error() == 87, "Could not verify child termination"
print("Background UI: private windows, zero input-desktop windows, real GL images and timeout cleanup passed")
