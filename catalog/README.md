# The firmware catalog

Firmwares built with OpenGP5 that anyone can install: OpenGP5 Launchpad and the web flasher list them. Each
firmware is one file here, `catalog/<id>.json`, added and updated by pull request.

The catalog is also the **registry of firmware ids**. A firmware's `id` (in its `firmware.json`) names its storage on
the pedal, so two firmwares with the same id would read each other's settings. A file here claims an id: a listing
with no releases yet (`"releases": []`) is fine, to claim one early. `reserved.json` lists ids nobody can use.

> **Firmwares listed here are not reviewed by OpenGP5.** The checks below make sure a download is the file its
> author listed, says what it is, and was built for a platform it says. They don't check what the code does. Install
> what you trust, and know how to [recover your pedal](../GETTING-STARTED.md#4-recovery-and-going-back-to-valetons-firmware).
> Listings from OpenGP5's own repositories are marked as such.

## How Launchpad and the web flasher use it

- **Launchpad** (Flash page, *More from the catalog*) downloads a release, checks its size and checksum against the
  catalog and what the image says it is, and only then offers to flash it. It keeps the list for when it's offline.
- **The web flasher** downloads OpenGP5's own listings itself. A web page can't download files from GitHub releases,
  so for those it links to the download; when you drop the file in, it recognises it by its checksum and says which
  listing it is (or warns that it isn't the file listed).
- Both show *Not reviewed by OpenGP5* and who made it, for every listing that isn't OpenGP5's, and hide a listing that
  is the same version as one they already ship.

## Listing your firmware

1. Build a release: with the [firmware template](https://github.com/OpenGP5/firmware-template), push a tag such as
   `v1.0.0` and its workflow publishes `GP-5_<id>.bin` on your repository's Releases page.
2. Add `catalog/<id>.json`:
   ```json
   {
     "id": "my-delay",
     "name": "My Delay",
     "author": "your name",
     "repo": "https://github.com/you/my-delay",
     "description": "A tape delay with wow and flutter. 4 presets.",
     "licence": "MIT",
     "releases": [
       {
         "version": "1.0.0",
         "platform": "0.8",
         "url": "https://github.com/you/my-delay/releases/download/v1.0.0/GP-5_my-delay.bin",
         "sha256": "…",
         "size": 123456
       }
     ]
   }
   ```
   `python3 tools/catalog.py release <file.bin> <url>` prints the release entry for a built image (version, platform,
   checksum, size, all read from the file).
3. Run `python3 tools/catalog.py check` and `python3 tools/catalog.py index` (it updates `docs/catalog.json`, the list
   Launchpad and the web flasher read), then open a pull request with both files.

CI checks the listing and downloads each new release to check it:
- the file name is the id;
- the id is free and not reserved;
- the release is on your own repository's Releases page;
- the checksum and size match;
- the image's own metadata says the same id, version and platform;
- the index is up to date.

A new release: add it at the top of `releases`. Keep older ones listed, so people can go back.

## Fields

| field | |
|---|---|
| `id` | the `id` in your `firmware.json`: lowercase letters, digits and `-` |
| `name`, `author`, `description` | shown in Launchpad and the web flasher (description: one or two sentences, at most 200 characters) |
| `repo` | your repository on GitHub, where the source and the releases are |
| `licence` | your firmware's licence (its SPDX name: `MIT`, `GPL-3.0-or-later`, ...) |
| `releases` | newest first: `version`, `platform` (the OpenGP5 API it was built for, `major.minor`), `url`, `sha256`, `size` |

Listings that break the [code of conduct](../CODE_OF_CONDUCT.md), that don't build from the repository's source, or
that harm pedals are removed.
