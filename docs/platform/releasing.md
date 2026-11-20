# Releasing your firmware

How to get a firmware from your computer to other people's pedals: give it a version, publish an image, and list it
in the catalog so Launchpad and the web flasher offer it.

## Before the first release

- **Your own repository**, best started from the [firmware template](https://github.com/OpenGP5/firmware-template).
  It builds and tests every push and publishes releases.
- **An `id` of your own** in `firmware.json`, never the template's `my-effect`. It names your firmware's storage on
  the pedal, so **keep it forever**: a new id makes everyone's saved settings disappear. Claim it early in the
  [catalog](https://github.com/OpenGP5/OpenGP5/blob/main/catalog/README.md) with a listing that has no releases yet.
- **A licence** (a `LICENSE` file, and its name in `firmware.json`).
- **Parameter names you're happy with.** Saved settings are matched by name, so renaming a parameter later starts it
  at its default for everyone. Adding, removing and reordering parameters is fine.

## A release

1. **Version:** raise `version` in `firmware.json`: `1.0.0` → `1.0.1` for fixes, `1.1.0` for new things, `2.0.0` for
   something incompatible. Pre-releases: `1.1.0-beta.1`.
2. **Platform:** move the `opengp5` submodule to the OpenGP5 release you built and tested with
   (for example `git -C opengp5 checkout v0.8.0-alpha`), and set `"platform"` to its `major.minor`.
3. **Test it:**
   - `./test.sh`: the simulator with your controls script;
   - `./build.sh`;
   - on the pedal: the heaviest settings while `gp5_link.py status` runs, a power-off and back (do the settings
     survive?), and an update from your last release (do they still?).
4. **Tag and push:** `git tag v1.0.1 && git push --tags`. The template's workflow builds the image and publishes it
   on your repository's Releases page as `GP-5_<id>.bin`.
5. **List it:** add the release to `catalog/<id>.json` in OpenGP5, newest first.
   `python3 tools/catalog.py release GP-5_<id>.bin <its download URL>` prints the entry from the image itself.
   Then run `python3 tools/catalog.py check` and `index`, and open a pull request. CI downloads your release and
   checks it.

## Writing the release notes

People flash this onto a pedal they love. Tell them:

- what's new and what's fixed, in plain words;
- whether their settings and presets carry over (they do, unless you renamed parameters or changed the id);
- what it was tested on;
- how to go back: the previous release stays listed, and recovery works as always.

## Good practice

- Build from a clean checkout of your tag. The template's CI does, and its image is byte-identical to a local build,
  so anyone can check it.
- Keep older releases listed in the catalog, so people can go back.
- Answer issues on your repository: bugs in a catalog firmware belong there, not in OpenGP5.
