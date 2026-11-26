# Firmwares written for OpenGP5 1.0, kept as they were

OpenGP5 1.x never breaks a firmware written for 1.0. These are the checks: the examples as they were released
with 1.0 (the tremolo, passthrough and the firmware template's overdrive), and NAM Loader 1.3.1 as released for 0.8
(the biggest audio link, 264 words: added 2026-11-24, when USB audio on by default would have broken it). Every change to the platform is tested
against them: `tools/test_compat.sh` builds and runs them in the simulator, and CI builds them for the pedal.

`budget.json` is what the platform itself may take of each memory for all of 1.x (its size at 1.0 plus a small
reserve for fixes); the rest is the firmware's, as `docs/platform/known-limits.md` promises. `tools/check_budget.py`
(in CI) builds the passthrough and fails if the platform takes more.

**Never edit these files.** If one stops building, the platform broke the promise: fix the platform, not the copy.
When OpenGP5 2.0 comes (if ever), add `compat/2.0` next to this folder.

(Taken at 1.0.0, 2026-11-24: the tremolo, passthrough and template as written for 1.0, with USB audio on by default and the tremolo with Bluetooth; budget.json measured then with USB audio and Bluetooth on. NAM Loader 1.3.1 stays as released for 0.8. Frozen from now on.)
