# Your own repository

The [firmware template](https://github.com/OpenGP5/firmware-template) starts a firmware in its own GitHub
repository, ready to build, test and release. It's a small overdrive (drive, tone, level, four presets) that you
change into yours.

## Start

1. On the template's page, click **Use this template** and create your repository.
2. Edit `firmware.json`: the firmware's `id`, `name`, `author`, `url` and `description`. The `id` names your
   firmware's storage on the pedal, so keep it once people use your firmware.
3. Build it, in the browser or on your computer:

   - **In the browser:** **Code › Codespaces › Create codespace**. When it has set itself up (a few minutes the
     first time), run `./test.sh` and `./build.sh` in its terminal. Nothing to install.
   - **On your computer** ([what you need](setup.md#what-you-need)):

     ```bash
     git clone --recursive <your repository>
     cd <your repository>
     opengp5/release/setup-toolchain.sh   # once
     ./test.sh                            # the whole firmware in the simulator
     ./build.sh                           # -> build/GP-5_<id>.bin
     ```

4. Flash `build/GP-5_<id>.bin` with Launchpad, the web flasher or `gp5tool.py`.

## What's in it

| File | What |
|---|---|
| `firmware.json` | Who it is and what it needs ([manifest](api.md#firmwarejson)) |
| `params.h` | Its settings: saved, in presets, sent to the DSP, editable from Launchpad ([Parameters](params.md)) |
| `dsp/firmware.h` | The sound, on the DSP: `fw_process()` gets 64 samples at a time |
| `nds32/firmware.c` | The controls and the screen, with the [UI kit](ui.md) |
| `test/controls.txt` | What `./test.sh` does in the [simulator](simulator.md) |
| `opengp5/` | The platform, a git submodule pinned to a release |

To move to a newer OpenGP5, check out its tag in the submodule: `git -C opengp5 checkout <tag>`.

## Built and released by GitHub

Every push is built and tested by GitHub Actions: the image and the simulator's screens are in the run's
**Artifacts**. Push a tag like `v1.0.0` and it publishes a release with the image. Then
[list it in the catalog](releasing.md) so Launchpad and the web flasher offer it.

## Licence

The template is MIT No Attribution: make your firmware from it under any licence you like, and put that licence's
name in `firmware.json`.
