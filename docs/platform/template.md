# Your own repository

Use the [firmware template](https://github.com/OpenGP5/firmware-template) to start your own GitHub repository.
It contains an overdrive with drive, tone, level and four presets. Change that effect to make your own.

## Start

1. On the template's page, click **Use this template** and create your repository.
2. Edit `firmware.json` to set `id`, `name`, `author`, `url` and `description`.
   The ID owns your firmware's storage. Keep it after release so updates can find saved data.
3. Build it, in the browser or on your computer:

   - **In the browser:** choose **Code › Codespaces › Create codespace**.
     Wait for setup to finish, then run `./test.sh` and `./build.sh` in its terminal.
   - **On your computer:** install the [required tools](setup.md#what-you-need).
     In the commands below, replace `<your repository>` with your clone URL in the first line and folder name in the second:

     ```bash
     git clone --recursive <your repository>
     cd <your repository>
     opengp5/release/setup-toolchain.sh   # once
     ./test.sh                            # the whole firmware in the simulator
     ./build.sh                           # -> build/GP-5_<id>.bin
     ```

4. Flash `build/GP-5_<id>.bin` with Launchpad, the web flasher or `gp5tool.py`.

## What's in it

| File | Purpose |
|---|---|
| `firmware.json` | Who it is and what it needs ([manifest](api.md#firmwarejson)) |
| `params.h` | Its settings: saved, in presets, sent to the DSP, editable from Launchpad ([Parameters](params.md)) |
| `dsp/firmware.h` | The sound, on the DSP: `fw_process()` gets 64 samples at a time |
| `nds32/firmware.c` | The controls and the screen, with the [UI kit](ui.md) |
| `test/controls.txt` | What `./test.sh` does in the [simulator](simulator.md) |
| `opengp5/` | A Git submodule: a copy of OpenGP5 pinned to one release |

The submodule keeps platform code separate from your effect. Its pinned release records the code you built against.
To update it, run `git -C opengp5 checkout <tag>` with the release tag you want. Test and commit the new submodule revision.

## Built and released by GitHub

GitHub Actions builds and tests each push. Find the image and screen previews under the run's **Artifacts**.
Push a tag such as `v1.0.0` to publish a release with the image. Then [list it in the catalog](releasing.md).

## Licence

The template uses the MIT No Attribution licence. Choose a licence for your firmware and put its name in `firmware.json`.
