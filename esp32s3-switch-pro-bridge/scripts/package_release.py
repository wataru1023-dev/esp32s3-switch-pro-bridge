#!/usr/bin/env python3
"""Package checked build artifacts and public sources without invoking a flasher."""

import argparse
import hashlib
import json
import re
import struct
import zipfile
from pathlib import Path


PROJECT = "esp32s3-switch-pro-bridge"
SOURCE_DIRS = ("main", "tests", "scripts", "docs")
SOURCE_FILES = (
    "CMakeLists.txt", "sdkconfig.defaults", "dependencies.lock", ".gitignore",
    "README.md", "README.zh-CN.md", "README.ja.md", "LICENSE", "CHANGELOG.md",
)


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def write_zip(path, entries):
    # Fixed metadata makes repeated packaging of the same build reproducible.
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name, data in sorted(entries.items()):
            info = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.create_system = 3
            info.external_attr = 0o100644 << 16
            archive.writestr(info, data)


def source_entries(root):
    entries = {}
    for relative in SOURCE_FILES:
        path = root / relative
        if not path.is_file():
            raise ValueError(f"Missing release source: {relative}")
        entries[f"{PROJECT}/{relative}"] = path.read_bytes()
    for directory in SOURCE_DIRS:
        for path in sorted((root / directory).rglob("*")):
            if not path.is_file() or path.is_symlink():
                continue
            relative = path.relative_to(root)
            if "__pycache__" in relative.parts or path.name == ".DS_Store" or path.suffix in (".pyc", ".pyo"):
                continue
            entries[f"{PROJECT}/{relative.as_posix()}"] = path.read_bytes()
    # Do not ship developer home paths or identifying serial numbers in sources.
    for name, data in entries.items():
        if re.search(rb"/Users/[A-Za-z0-9_.-]+/", data):
            raise ValueError(f"Private local path in release source: {name}")
    return entries


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=root / "build")
    parser.add_argument("--output-dir", type=Path)
    args = parser.parse_args()
    build = args.build_dir.resolve()
    cmake = (root / "CMakeLists.txt").read_text()
    match = re.search(r'set\(PROJECT_VER\s+"([0-9]+\.[0-9]+\.[0-9]+)"\)', cmake)
    if not match:
        raise ValueError("PROJECT_VER must use a numeric semantic version")
    version = match.group(1)
    description = json.loads((build / "project_description.json").read_text())
    flasher = json.loads((build / "flasher_args.json").read_text())
    if description["project_version"] != version or description["target"] != "esp32s3":
        raise ValueError("Build target/version does not match release sources; rebuild first")

    expected = {
        "0x0": ("bootloader.bin", "bootloader/bootloader.bin"),
        "0x8000": ("partition-table.bin", "partition_table/partition-table.bin"),
        "0x10000": ("app.bin", f'{description["project_name"]}.bin'),
    }
    if flasher["flash_files"] != {offset: original for offset, (_, original) in expected.items()}:
        raise ValueError("Unexpected flash layout; refusing to package hardcoded offsets")
    if flasher["flash_settings"] != {"flash_mode": "dio", "flash_size": "16MB", "flash_freq": "80m"}:
        raise ValueError("Firmware package requires the tested N16R8 DIO/80 MHz/16 MB settings")
    sources = source_entries(root)
    app_path = build / f'{description["project_name"]}.bin'
    build_inputs = [root / "CMakeLists.txt", root / "sdkconfig.defaults", root / "dependencies.lock"]
    config_file = Path(description["config_file"])
    build_inputs.append(config_file if config_file.is_absolute() else build / config_file)
    build_inputs += [path for path in (root / "main").rglob("*") if path.is_file()]
    if any(path.stat().st_mtime_ns > app_path.stat().st_mtime_ns for path in build_inputs):
        raise ValueError("Sources/configuration changed after the application build; rebuild first")
    firmware = {name: (build / original).read_bytes() for name, original in expected.values()}
    app = firmware["app.bin"]
    # ESP application descriptor is at image-header(24) + segment-header(8).
    if len(app) < 288 or struct.unpack_from("<I", app, 32)[0] != 0xABCD5432:
        raise ValueError("Invalid ESP-IDF application image")
    if app[48:80].split(b"\0", 1)[0].decode() != version:
        raise ValueError("Application binary version is stale; rebuild first")
    idf_version = app[144:176].split(b"\0", 1)[0].decode()
    if not idf_version.startswith("v5.3.3"):
        raise ValueError(f"Expected tested ESP-IDF v5.3.3, got {idf_version}")
    config = (build / "config/sdkconfig.h").read_text()
    for setting in ("CONFIG_SPIRAM_MODE_OCT 1", "CONFIG_SPIRAM_SPEED_80M 1", "CONFIG_SPIRAM_USE_MEMMAP 1"):
        if f"#define {setting}" not in config:
            raise ValueError(f"N16R8 firmware configuration missing {setting}")

    notes_path = root / "docs" / f"release-v{version}.md"
    notes = notes_path.read_bytes().replace(
        f"(release-audit-v{version}.md)".encode(), b"(RELEASE_AUDIT.md)"
    )
    audit = (root / "docs" / f"release-audit-v{version}.md").read_bytes()
    firmware["flash_args"] = (
        "--flash_mode dio --flash_freq 80m --flash_size 16MB\n"
        "0x0 bootloader.bin\n0x8000 partition-table.bin\n0x10000 app.bin\n"
    ).encode()
    firmware["LICENSE"] = (root / "LICENSE").read_bytes()
    firmware["RELEASE_NOTES.md"] = notes
    firmware["RELEASE_AUDIT.md"] = audit
    firmware["README.md"] = f"""# {PROJECT} v{version} — ESP32-S3 N16R8 firmware

Requires **16 MB flash and 8 MB octal PSRAM**. Built with ESP-IDF {idf_version}.
Do not flash this binary to boards with another flash/PSRAM configuration.

## Flash / 烧录

Exit the serial monitor first (Ctrl + ]). Activate ESP-IDF 5.3.3, change to this
extracted firmware directory, and replace PORT with your actual flashing port:

```sh
python -m esptool --chip esp32s3 --port PORT --baud 460800 --before default_reset --after hard_reset write_flash @flash_args
```

This writes the bootloader at 0x0, partition table at 0x8000 and app at 0x10000.
It does not erase NVS; the saved BLE target and USB report rate remain intact.
This preservation assumes the existing firmware uses the same partition layout.
No erase_flash command or merged full-flash image is needed for this update.

烧录前退出串口监视器，激活 ESP-IDF v5.3.3，在本目录运行上述指令，将 PORT
替换为实际烧录口。三个分区独立写入，保留相同分区布局下的 BLE 配对目标和上报率。

After flashing, connect the board's native USB/OTG data port to the Mac and replug
it to let the host reload calibration. The USB-UART flashing port and native HID
port are separate on dual-port boards. A disconnected UART monitor does not
indicate a controller failure.

烧录后将板子的原生 USB/OTG 数据口连到 Mac，并重新插拔以加载校准。双口开发板
的烧录串口与手柄口不同；串口断开导致 monitor 等待重连，不影响独立的手柄接口。

Default logs are quiet. Debugging uses the UART0 console at 115200 baud:
`debug on`, `axis debug on 32`, `raw debug on 32`; use corresponding `off` to stop.
See RELEASE_NOTES.md for the changes and verification limits.
""".encode()
    source_checksums = {name: sha256(data) for name, data in sorted(sources.items())}
    firmware["manifest.json"] = (json.dumps({
        "version": version,
        "chip": "esp32s3",
        "hardware": "N16R8: 16 MB flash, 8 MB octal PSRAM",
        "idf_version": idf_version,
        "flash_settings": flasher["flash_settings"],
        "images": {offset: {"file": name, "sha256": sha256(firmware[name])} for offset, (name, _) in expected.items()},
        "sources": source_checksums,
    }, indent=2, ensure_ascii=False) + "\n").encode()
    firmware["SHA256SUMS"] = "".join(f"{sha256(data)}  {name}\n" for name, data in sorted(firmware.items())).encode()

    output = (args.output_dir or root / "releases" / f"v{version}").resolve()
    output.mkdir(parents=True, exist_ok=True)
    source_name = f"{PROJECT}-v{version}-source.zip"
    firmware_name = f"{PROJECT}-v{version}-n16r8-firmware.zip"
    write_zip(output / source_name, sources)
    firmware_prefix = f"{PROJECT}-v{version}-n16r8/"
    write_zip(output / firmware_name, {firmware_prefix + name: data for name, data in firmware.items()})
    (output / "RELEASE_NOTES.md").write_bytes(notes)
    (output / "RELEASE_AUDIT.md").write_bytes(audit)
    release_files = {name: (output / name).read_bytes() for name in (
        source_name, firmware_name, "RELEASE_NOTES.md", "RELEASE_AUDIT.md"
    )}
    checksums = "".join(f"{sha256(data)}  {name}\n" for name, data in sorted(release_files.items())).encode()
    (output / "SHA256SUMS").write_bytes(checksums)
    release_files["SHA256SUMS"] = checksums
    bundle = output / f"{PROJECT}-v{version}-release.zip"
    write_zip(bundle, release_files)
    print(f"Release bundle: {bundle}")
    print(f"SHA-256: {sha256(bundle.read_bytes())}")
    print(f"Source files: {len(sources)}; firmware size: {len(app)} bytes")


if __name__ == "__main__":
    main()
