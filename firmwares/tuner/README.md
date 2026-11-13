# Strobe tuner: a demo firmware built with OpenGP5

The guitar (or bass) goes straight through; the screen is a strobe tuner.

| control | does |
|---|---|
| foot switch | mute on / off, to tune silently (LED mint = sound on, dim red = muted) |
| PARA turn | reference pitch A4, 430–450 Hz, 1 Hz per click (kept across power-off) |
| PARA push | back to 440 Hz |
| volume knob | output level |

**Reading it:** the note and how many cents off it is; below, the strobe. Its three bands show the
fundamental, the 2nd and the 4th harmonic as light and dark segments: they stand still when the string is in
tune, drift right when it is sharp and left when it is flat, faster the further off it is. Notes from a
5-string bass's low B (31 Hz) up to about 1 kHz.

## How it works

- **DSP** (`dsp/tuner.h`): YIN pitch detection on the signal low-passed and decimated to 5.5 kHz picks the note.
  The strobe works like a mechanical one: a phase counter turns once per period of the note's exact frequency,
  each band band-passes one harmonic, and 64 segments record how long the harmonic was above zero while the
  counter passed them. The cents come from the fundamental's phase drift against a reference oscillator.
  About 23 % of the DSP's time on average.
- **Control processor** (`nds32/firmware.c`): draws the note, the cents and the strobe, and handles the controls.
- **Between them:** the audio link (`gp5_link_start` / `GP5_AUDIO_LINK`): every block the DSP sends the note,
  the cents and the 3 × 64 strobe segments (`dsp/link_words.h`) and receives the reference pitch and the mute.

Lettering: Rubik SemiBold (`nds32/font_aa.h`, made with `tools/make_font.py`), SIL Open Font License
(`LICENSE-Rubik-OFL.txt`).

## Tests on the computer

```
firmwares/tuner/test/run.sh                 # 120 test tones (bass and guitar strings, in tune and off): note,
                                            # cents and the strobe's drift checked
firmwares/tuner/test/run.sh wav guitar.wav  # what it reads from a recording, every 0.1 s
firmwares/tuner/test/screen.sh              # the screen as PNGs in build/tuner_screen
```
