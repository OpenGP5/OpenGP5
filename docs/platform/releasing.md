# Releasing your firmware

To share a build, give it a version, publish the image and add it to the catalog.
Users can then find it in Launchpad and the web flasher.

## Before the first release

- Start a repository with the [firmware template](https://github.com/OpenGP5/firmware-template).
- Choose your own `id` in `firmware.json`. Do not release with the template's `my-effect` ID.
  Keep your ID after release: changing it starts users with empty storage.
- Claim the ID in the [catalog](https://github.com/OpenGP5/OpenGP5/blob/main/catalog/README.md).
  You can submit a listing before adding releases.
- Add a `LICENSE` file and name the licence in `firmware.json`.
- Choose setting names you can keep. A saved value uses the setting's name as its key.
  A rename gives that setting its default value. You can add, remove or reorder other settings without resetting names that still match.

## A release

### 1. Set the version

Update `version` in `firmware.json`:

| Change | Example |
|---|---|
| Fix | `1.0.0` → `1.0.1` |
| New feature that keeps compatibility | `1.0.0` → `1.1.0` |
| Incompatible change | `1.0.0` → `2.0.0` |
| Test release | `1.1.0-beta.1` |

### 2. Pin the platform

Use the OpenGP5 release you tested. Check it out in the `opengp5` folder, for example
`git -C opengp5 checkout v1.0.0`. Set `platform` in your manifest to the release's major and minor number,
such as `"1.0"`. Commit the pinned copy with your changes.

### 3. Test the build

Run `./test.sh` to check scripted controls in the simulator, then `./build.sh` to make the image.
On a pedal, check these cases:

- Play with the settings that use the most time while `gp5_link.py status` reports load.
- Power off and on. Check that saved settings return.
- Update from your last release. Check that saved data still loads.

### 4. Tag and publish

Make a tag that matches the version with `git tag v1.0.1`, then push it with `git push origin v1.0.1`.
The template uses GitHub Actions to build the image and put `GP-5_<id>.bin` on your Releases page.
It also runs tests on each push. This is continuous integration (CI).

### 5. Update the catalog

Add the release to `catalog/<id>.json` in OpenGP5, newest first.
Run `python3 tools/catalog.py release GP-5_<id>.bin <download-url>` to print a listing from the image.
Use your build's filename and download link in the command.

Run `python3 tools/catalog.py check`, then `python3 tools/catalog.py index`.
Open a pull request with the listing changes. CI downloads the image and checks it against the listing.

## Writing the release notes

Tell users:

- what changed and which faults the release fixes;
- whether settings and presets carry over, including any renamed settings or changed IDs;
- which pedal and software versions you tested;
- how to install the previous release and where to find the [recovery steps](../../GETTING-STARTED.md#recovery).

Tell users about the known DSP update fault. It can block a USB flash, so link to the steps for that case.

## Good practice

Build from a clean copy of the release tag. The template's CI uses that source so users can check the same build.
Keep old builds in the catalog so users can go back.
Handle bugs in your effect in its own project. Report faults in the shared code to OpenGP5.
