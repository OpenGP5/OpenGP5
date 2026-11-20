---
title: OpenGP5
description: Open firmware for the Valeton GP-5 guitar pedal, and a framework for writing your own.
---

{{< blocks/cover image_anchor="center" height="auto" color="dark" >}}
<img class="og-hero-banner" src="assets/opengp5-banner.webp" alt="OpenGP5">
<p class="lead mt-4 mb-4">Open firmware for the Valeton GP-5 guitar pedal, written from scratch, and a small framework for writing your own.</p>
<a class="btn btn-lg btn-primary me-3 mb-3" href="docs/get-started/">Get started</a>
<a class="btn btn-lg btn-outline-light me-3 mb-3" href="flasher/">Flash from your browser</a>
{{< /blocks/cover >}}

{{% blocks/section color="white" type="row" %}}

{{% blocks/feature icon="fa-solid fa-guitar" title="Play" url="docs/firmwares/" url_text="See the firmwares" %}}
Play NAM captures with their own cab IRs, or Dreamcatcher's micro-looper, glitch delay and reverb. Each is a
complete firmware for the pedal.
{{% /blocks/feature %}}

{{% blocks/feature icon="fa-solid fa-laptop" title="Manage" url="docs/launchpad/" url_text="Meet Launchpad" %}}
Launchpad, the desktop app, flashes firmware, loads your captures and IRs, edits presets live, and backs everything
up, on macOS, Windows and Linux.
{{% /blocks/feature %}}

{{% blocks/feature icon="fa-solid fa-code" title="Build" url="docs/platform/" url_text="Write your first effect" %}}
Write an effect in plain C: your sound, your screen, your controls. Hear it on your computer before you flash it.
{{% /blocks/feature %}}

{{% /blocks/section %}}

{{% blocks/section color="light" %}}

## An effect is a few lines of C

The sound of a tremolo, the example firmware:

```c
static void fw_process(const int32_t *in, int32_t *out_left, int32_t *out_right)
{
    for (uint32_t i = 0; i < GP5_BLOCK; i++) {                    /* 64 samples at a time */
        float wave = 0.5f + 0.5f * sinf(2.0f * GP5_PI * phase);
        float gain = 1.0f - depth * wave;
        phase += step; if (phase >= 1.0f) phase -= 1.0f;
        out_left[i] = out_right[i] = (int32_t)((float)in[i] * gain);
    }
}
```

The platform handles everything else: both processors, USB, the screen, the knobs and foot switch, saving your
settings and presets, and an editor for them on the computer. [How a firmware works →](docs/platform/concepts/)

{{% /blocks/section %}}

{{% blocks/section color="white" %}}

## Before you flash

OpenGP5 is in **alpha**. It has run on several GP-5s without a problem reported, and the pedal's factory loader
can't be overwritten over USB, so a bad flash is usually easy to recover from. In rare cases recovery needs a USB
adapter soldered to the pedal's debug pads. Read [Before you flash](docs/get-started/safety/) first.

Not affiliated with or endorsed by Valeton. "Valeton" and "GP-5" are Valeton's trademarks, used only to say which
pedal this runs on.

{{% /blocks/section %}}
