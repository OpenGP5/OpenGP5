# Storage on the GP-5

The installed build owns the pedal's saved data. A build with a different ID starts with no data.
An update with the same ID keeps it. If you rename a setting, that setting can reset to its default.

Back up before switching. Launchpad offers a backup when it can read the running firmware.
A backup belongs to the firmware that made it; install that firmware before you restore the data.

## Who owns it

Before `fw_init()`, the platform reads `GP5O`, the record of who owns the data.
It checks that record against the string `id` in `firmware.json`:

- If the ID matches, the platform keeps the data.
- If another ID owns the area, the platform clears it and marks it with the new ID.
- If the area has no mark, the platform keeps the data and adds the mark.

Older builds used `fw_name()` to claim storage. Since 0.5.3, the default name `opengp5` does not claim storage.
A mark with that default name from 0.5.2 counts as no mark. This lets a later named build keep that data.

Launchpad also clears storage when switching firmware. From a running OpenGP5 build, it does this **before flashing**.
From Valeton's firmware, it clears the area after the new build starts.
Back up before you start, since a flash failure does not undo a storage erase.

### Returning to Valeton

Launchpad clears the shared area and writes Valeton's preset-order table at `0x170000`.
The table is 100 bytes, numbered 0 through 99. Without it, Valeton's firmware can refuse to save presets.

Valeton then shows its **factory presets**. Restore your preset backup to get your saved presets back.
Import exported SnapTones and user IRs in Valeton Suite; Launchpad cannot back them up.

## The map (control processor flash)

| Flash | Size | What |
|---|---|---|
| 0x000000–0x00FFFF | 64 KB | factory loader (USB updates leave it in place) |
| 0x010000–0x13EFFF | ≤ 1.2 MB | the firmware's code (OpenGP5 builds 60–110 KB; Valeton's 1.2 MB) |
| **0x13F000–0x15FFFF** | 132 KB | **files**, run A (33 sectors) |
| **0x160000–0x161FFF** | 8 KB | **settings** (`gp5_store`) |
| **0x162000–0x189FFF** | 160 KB | **files**, run B (40 sectors). 0x170000–0x189FFF is Valeton's user-preset area when Valeton's firmware is installed |
| 0x190000–… | | Valeton's factory presets, models, global settings (0x1D0000): outside the platform storage area |

The shared storage spans `0x13F000–0x189FFF`, about 300 KB.
Storage-write commands cannot write outside that range.

## Settings: small, saved often

Use `gp5_store_save(id, data, len)` and `gp5_store_load(id, …)` for records up to 1 KB.
These records suit presets, options and the last selected item.
The store appends records to a log across two 4 KB sectors to spread flash wear.

## Files: larger data

Use files for captures, samples and IRs that a computer writes.
Each file has a numeric firmware ID and a name of up to 31 ASCII characters, such as `tone-03`.
This numeric file ID is separate from the firmware's string ID in `firmware.json`.

A file occupies whole 4 KB sectors, in one consecutive run. There are 73 file sectors across two runs.
The firmware can read the data from flash without copying the whole file into RAM.

Each file starts with a 64-byte header. Data starts at offset `+64`:

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

### Write, delete and read files

The computer writes a new copy in this order:

1. Find and erase a free run of sectors.
2. Write the data, then write the header last.
3. Read the new file back to check it.
4. Erase the first sector of the old copy, if one exists.

Writing the header last lets readers reject an incomplete new copy after a power cut.
Deleting a file erases its header sector. Firmware reads files with `gp5_file()`, `gp5_file_check()` and `gp5_file_each()`.

After you write files or settings, send `7E 0E <id>`.
The platform reads the saved settings and presets, then calls `fw_storage_changed()` to load other data.

## Backups

A format 2 backup names the build it came from. It saves that build's files, settings and presets.
The app rejects a backup from a different build, or one that lacks a build ID.
It checks the ID again while it holds the pedal link for the restore.

A backup holds data, not a firmware installer. See [Launchpad Backups](https://opengp5.github.io/docs/launchpad/backups/) for the supported data types.
