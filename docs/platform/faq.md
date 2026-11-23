# Questions

## Using it

**Can I go back to Valeton's firmware?** Yes. Flash Valeton's GP-5 update with Launchpad, the web flasher or
`gp5tool.py flash <file> --sections all`. Valeton's presets come back. Export your SnapTones and user IRs in Valeton
Suite before you leave Valeton's firmware: OpenGP5's tools can't back those up.

**Will it brick my pedal?** Usually a bad flash is fixable: unplug, hold the foot switch and PARA, plug in, and
flash again. The factory loader that does this can't be overwritten over USB. Some pedals stall every update, Valeton's own
too: they need a debug adapter wired inside the pedal once ([the J7 fix](j7-recovery.md)). It's alpha software: only flash if you
accept that risk.

**Does it use Valeton's amp models, Bluetooth or presets?** No. OpenGP5 firmwares are written from scratch and use
none of Valeton's code or data. Bluetooth isn't used.

**Is my firmware's data kept when I install another one?** No: the pedal's storage belongs to the installed firmware,
and installing a different one starts empty. Back it up first; Launchpad offers to, every time.

**Which firmwares are there?** The ones in Launchpad and the web flasher: OpenGP5's own (Dreamcatcher, NAM Loader,
the tremolo) and whatever is listed in the [catalog](https://github.com/OpenGP5/OpenGP5/blob/main/catalog/README.md).
Listings not from OpenGP5 aren't reviewed.

## Writing firmware

**Do I need the pedal to start?** No. The [simulator](simulator.md) runs a whole firmware on your computer, screen
and sound included, and the [template](https://github.com/OpenGP5/firmware-template) builds in GitHub Codespaces
without installing anything.

**What language?** C (C99), on both processors. Neither has a C library: the platform provides maths (`gp5_math.h`),
`printf`-style formatting (`gp5_fmt.h`) and everything else a firmware needs.

**Which processor should my sound code run on?** Start on the DSP: it's simpler. If it doesn't fit, move the heavy
part to the control processor over the [audio link](audio-link.md); [performance](performance.md) helps decide.

**How much can it do?** Roughly: a neural amp model and a 1,024-tap cab IR (NAM Loader), or a looper, grain delay and
reverb (Dreamcatcher), each split across both processors. A simple effect on the DSP alone uses a few percent.

**Can I use C++, a C library, or another language?** Not with the platform's build. C with no standard library is
what both processors run. Many C libraries need only `<stdint.h>` and maths and port easily; anything that needs
`malloc`, files or threads doesn't.

**Can I use code from Valeton's firmware or the chip vendor's SDK?** No: nothing from either goes into OpenGP5 or the
catalog, so everything stays freely shareable.

**How do settings get saved?** Declare them as parameters and the platform saves them, keeps presets, and lets
Launchpad edit them ([parameters](params.md)). For other data: [storage](storage.md).

**Stereo in?** The pedal's input is mono (the guitar); the outputs are stereo.

**48 kHz?** Yes: `"audio": { "sample_rate": 48000 }` in `firmware.json`. Blocks are then 1.33 ms.

**How do I share my firmware?** [Releasing your firmware](releasing.md).

## The project

**Who makes OpenGP5?** A few people in their spare time, publishing as OpenGP5. It's not affiliated with Valeton.

**How can I help?** Try it and send a [hardware report](https://github.com/OpenGP5/OpenGP5/issues/new?template=hardware.yml),
whether it worked or not. Also: write a firmware, improve the docs, or see [CONTRIBUTING](../../CONTRIBUTING.md).
