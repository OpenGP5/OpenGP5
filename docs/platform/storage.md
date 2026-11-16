# Storage on the GP-5 (OpenGP5 platform 0.5)

**The pedal's storage belongs to the firmware that is installed.** Installing a different firmware starts with
empty storage; going back to Valeton's firmware gives Valeton's preset area back. Keeping presets, tones or settings
across firmware changes is up to the user: OpenGP5 Launchpad offers a backup before every firmware change, marks each
backup with the firmware it came from, and only restores a backup onto that firmware.

## The map (control processor flash)

| Flash | Size | What |
|---|---|---|
| 0x000000–0x00FFFF | 64 KB | factory loader (never erased) |
| 0x010000–0x13EFFF | ≤ 1.2 MB | the firmware's code (OpenGP5 firmwares 60–110 KB; Valeton's 1.2 MB) |
| **0x13F000–0x15FFFF** | 132 KB | **files**, run A (33 sectors) |
| **0x160000–0x161FFF** | 8 KB | **settings** (`gp5_store`) |
| **0x162000–0x189FFF** | 160 KB | **files**, run B (40 sectors). 0x170000–0x189FFF is Valeton's user-preset area when Valeton's firmware is installed |
| 0x190000–… | | Valeton's factory presets, models, global settings (0x1D0000): never touched by OpenGP5 |

The storage area is 0x13F000–0x189FFF (300 KB). The platform lets the firmware and the computer write nowhere else.

## Who owns it

At start-up, before `fw_init()`, the platform checks the claim (settings record `GP5O` = the firmware's
`fw_name()`):

- **the same name:** nothing happens (updating a firmware keeps its data);
- **another name:** the whole area is wiped and the mark becomes this firmware's;
- **no mark** (a fresh pedal, or a firmware from before 0.5): the data is kept and marked.

A firmware without its own `fw_name()` gets the platform's default, `opengp5`. That name doesn't claim the storage (0.5.3):
such a firmware doesn't mark the area, and a stored `opengp5` mark (written by 0.5.2) counts as no mark. So a
later build of the same firmware that does name itself keeps the data.

OpenGP5 Launchpad also wipes the area when it installs a different firmware (before flashing, through the running
OpenGP5 platform; coming from Valeton's firmware, right after), so firmwares from before 0.5 behave the same.

**Back to Valeton:** Launchpad erases the area and writes Valeton's preset order table (100 bytes 0, 1, … 99 at
0x170000), as on a pedal that never left stock. Valeton's firmware then shows its factory presets (erased user
slots read as factory ones) and saves normally. Without the table it would silently refuse to save presets.

## Settings: small, saved often

`gp5_store_save(id, data, len)` / `gp5_store_load(id, …)`: up to 1 KB per record, appended to a log in two 4 KB
sectors so frequent saves don't wear one place. For presets, options, the last selection.

## Files: bigger, written rarely

Captures, samples, IRs. A file has a firmware id, a name (up to 31 ASCII characters, e.g. `tone-03`), and occupies
whole 4 KB sectors, consecutive within one run, so a firmware reads it straight from flash. 73 sectors in all.

Each file starts with a 64-byte header in its first sector; the data follows at +64:

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | magic `GP5F` (0x46355047) |
| 4 | 4 | sequence number: the newest copy of a firmware id + name wins |
| 8 | 4 | firmware id |
| 12 | 4 | data length in bytes |
| 16 | 4 | CRC-32 (zlib) of the data |
| 20 | 2 | sectors the file occupies (header + data) |
| 22 | 2 | flags, 0 |
| 24 | 32 | name, ASCII, zero-padded |
| 56 | 4 | CRC-32 of bytes 0–55 |
| 60 | 4 | 0xFFFFFFFF |

**Writing** (the computer): first free run of sectors, erase, data, then the header **last**, read back, then erase
the first sector of any older copy. A power cut leaves the old file or the new one, never a broken one.
**Deleting** erases the header sector. **Reading** (firmware): `gp5_file()`, `gp5_file_check()`, `gp5_file_each()`.
After the computer changes a firmware's files or settings it sends `7E 0E <id>`; `fw_store_changed(id)` reloads.

## Backups

A backup (Launchpad format 2) holds only the installed firmware's own data: its files, its settings record, its
tones or presets. It records which firmware it came from. Launchpad restores it only onto that same firmware: a
backup from another firmware, or an old backup that doesn't say where it came from, is refused. Launchpad checks
again with the pedal locked just before it writes anything.
