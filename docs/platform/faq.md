# Questions

## Using it

**Can I go back to Valeton's firmware?** Yes. Follow [Recovery](../../GETTING-STARTED.md#recovery) to flash the full update.
The pedal starts with **factory presets**. Restore a Launchpad backup to get your own presets back.
Before leaving Valeton's firmware, export SnapTones and user IRs in Valeton Suite; Launchpad cannot back them up.

**Can a flash leave the pedal unable to start?** A failed flash can stop the installed app from starting.
The factory loader remains available for USB recovery. Some pedals also have a DSP update fault that blocks a new flash,
including Valeton's updates. Those pedals may need a debug adapter wired inside the case.
Read [Known limits](known-limits.md) and [J7 recovery](j7-recovery.md) before deciding to flash alpha software.

**Does OpenGP5 use Valeton's models or presets?** No. The firmware contains no code or data from Valeton or the chip vendor.

**Does my data survive a change of firmware?** A different firmware ID starts with empty storage.
Back up before switching. Launchpad offers a backup if it can read the installed build.
An update with the same ID keeps storage; check its release notes for changes to saved settings.

**Which effects can I try?** OpenGP5 provides NAM Loader, Dreamcatcher and the tremolo.
See the [firmware guides](https://opengp5.github.io/docs/firmwares/) for controls and downloads.
Other authors can list builds in the [catalog](https://github.com/OpenGP5/OpenGP5/blob/main/catalog/README.md).
Those builds carry *Not reviewed*: file checks do not include a source-code review.

## Writing firmware

**Do I need a pedal to start?** No. The [simulator](simulator.md) runs the code and saves sound and screen files.
The [template](https://github.com/OpenGP5/firmware-template) can build in GitHub Codespaces, in your browser.
You need a pedal to check timing and hardware behavior.

**Which platform version should I use?** These docs target platform 0.8.0-alpha.
The planned 1.0 API began in 0.7, so a guide's “1.0 API” label does not mean a 1.0 platform release exists.
See [Versions](api.md#versions).

**What language can I use?** The build uses C99 on both chips, with no standard C library.
The platform supplies maths (`gp5_math.h`) and `printf`-style text formatting (`gp5_fmt.h`).

**Where should I put the sound code?** Start on the DSP, the digital signal processor.
If it runs out of time or memory, move work to the control chip through the [audio link](audio-link.md).
Use [Performance](performance.md) to measure each side.

**How much sound processing fits?** NAM Loader runs an amp model and a 1,024-tap cab IR.
Dreamcatcher runs a looper, grain delay and reverb. Both split work across the chips.
A small tremolo on the DSP uses a few percent of its time budget.

**Can I use C++ or a C library?** The platform build supports C.
Code that needs only integer types and maths may need few changes to run here.
Code that depends on `malloc`, host files or threads needs more work; the platform does not provide those calls.

**Can I include code from Valeton or the chip vendor's SDK?** No.
OpenGP5 and its catalog exclude code, headers, libraries, firmware and data from those sources.

**How do I save settings?** Declare [parameters](params.md) and mark which ones to save or include in presets.
The platform handles saving and lets Launchpad edit them. For other data, use [Storage](storage.md).

**Does the pedal have stereo input?** No. The guitar input is mono. The outputs are stereo.

**Can I use 48 kHz?** Yes. Set `"audio": { "sample_rate": 48000 }` in `firmware.json`.
Each 64-sample block then lasts 1.33 ms.

**How do I share my build?** Follow [Releasing your firmware](releasing.md).

## The project

**Who makes OpenGP5?** A few people work on it in their spare time under the OpenGP5 name.
The project has no affiliation with Valeton.

**How can I help?** Send a [hardware report](https://github.com/OpenGP5/OpenGP5/issues/new?template=hardware.yml)
with successes and faults. You can also write an effect or improve the docs.
See [Contributing](../../CONTRIBUTING.md).
