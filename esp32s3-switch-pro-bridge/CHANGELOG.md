# Changelog

## 5.9.15 — 2026-10-10

Includes the 5.9.14 macOS analog-stick calibration fix and the release review hardening:

- Return complete Switch Pro calibration data and the proper subcommand ACKs; retain queued
  initialization replies until USB completion, retry failures, and count completed input reports.
- Validate HID lengths, SPI reply capacity and USB control-transfer direction; discard stale vendor
  replies when a USB session is reset.
- Isolate BLE reconnect callbacks with connection generations and schedule subscription work in
  the NimBLE host; synchronize calibration and avoid learning centers from diagnostic reads.
- Reject oversized console lines and targets, parse parameters strictly, escape JSON and report
  real operation / NVS errors.
- Synchronize configuration access, serialize NVS saves, and update runtime values only after
  a successful commit. Use the ESP-IDF application description as the firmware version source.
- Disable continuous diagnostic logs by default, preserve runtime DEBUG support, and use the
  UART console without a secondary native USB serial console.
- Recognize standard neutral rumble, forward the vibration fields in subcommand reports, and
  consume stop packets only after a successful BLE write from the same stop request.
- Add host regression coverage, release packaging, checksums and updated multilingual instructions.

The owner confirmed working buttons and sticks on **5.9.14**. **5.9.15 has not been flashed to
hardware**; build / host test evidence and remaining verification limits are in the
[release notes](docs/release-v5.9.15.md) and [audit record](docs/release-audit-v5.9.15.md).

## 5.9.14 — 2026-10-09 (local diagnostic build)

- Fixed the missing Switch Pro calibration parameters and incorrect device / SPI ACK types.
- Queued USB initialization replies and retried endpoint-busy, submission and completion failures.
- Added automatic axis and USB diagnostics during the macOS investigation.
- The project owner flashed this version and reported that both sticks and buttons worked.

## Earlier public source

The GitHub `main` baseline for this update is `dd7312505aace3ef12051aa33d0d70060a52921f`.
Its CMake project version is `5.9.8`; its console version string was `5.9.13`.
No GitHub tag or Release existed at the time of this review. Those strings do not establish
separate public release history.
