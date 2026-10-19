## What this changes, and why

<!-- Describe the observable behavior and link a relevant issue, if any. -->

## Validation

<!-- For every applicable check, give the exact command and result below.
     Leave boxes unchecked for unrun/not-applicable checks and explain why.
     Redact secrets, setup/pairing QR codes, and personal/network identifiers. -->

- [ ] Host suite: `test/run-all.sh`, with release tooling and the pinned distribution validator; report passes and skips
- [ ] Firmware change: both `viewe15` and `viewe21` build on ESP-IDF v5.5.4 with each build directory's own sdkconfig; review any intended `dependencies.lock` changes
- [ ] UI/UX change: `python test/ui_sim/tooling.py` and `test/ui_sim/run-all.sh` pass; affected screenshots inspected at both panel sizes
- [ ] Artwork change: reproducibility and artwork tests pass; sources, generated files, and provenance included

**Commands, results, and reasons for unrun/not-applicable checks:**


**Affected screens and interactions (or "none"):**


**Hardware evidence, if applicable:**
<!-- Name the board/module revision, firmware and controller versions, steps,
     and observations. Label simulator evidence separately; it cannot establish
     physical compatibility, radio behavior, power, or timing on a real device. -->
