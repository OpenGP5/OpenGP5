---
title: OpenGP5
description: Open firmware for the Valeton GP-5 guitar pedal, and a framework for writing your own.
---

<section class="og-hero">
  <div class="og-hero__inner">
    <div class="og-hero__text">
      <h1 class="og-hero__title"><img src="wordmark.png" alt="OpenGP5" width="740" height="159"></h1>
      <p class="og-hero__lead">Open firmware for the Valeton GP-5 guitar pedal. Play amp models with Neural Amp Modeler (NAM),
      loop and shape your sound, or write an effect in C.</p>
      <div class="og-hero__actions">
        <a class="btn btn-lg og-btn-primary" href="docs/get-started/">Get started</a>
        <a class="btn btn-lg og-btn-ghost" href="flasher/">Flash from your browser</a>
      </div>
    </div>
    <figure class="og-pedal" aria-label="A GP-5 showing the screens of NAM Loader, Dreamcatcher and the tremolo">
      {{< og-pedals >}}
      <figcaption class="og-pedal__caption">
        <span class="og-screen">NAM Loader</span><span class="og-screen">Dreamcatcher</span><span class="og-screen">Tremolo</span>
      </figcaption>
    </figure>
  </div>
</section>

<section class="og-paths">
  <div class="og-wrap">
    <a class="og-path" href="docs/firmwares/">
      <svg class="og-icon" viewBox="0 0 24 24" aria-hidden="true"><!-- Lucide "guitar-electric", ISC licence --><path d="m12.172 11.828 4.242-4.242" /><path d="M16.414 7.586A2 2 0 0017 6.172V5a2 2 0 011.177-1.823L20.59 2.09a1 1 0 011.32 1.32l-1.088 2.412A2 2 0 0119 7h-1a2 2 0 00-1.586.586" /><path d="m6 16 2 2" /><path d="M8.23 9.85a3 3 0 011.28-1.454 1 1 0 011.496.815 4 4 0 003.783 3.783 1 1 0 01.815 1.496 3 3 0 01-1.454 1.28l-.92.38A2 2 0 0012 18a4 4 0 01-4 4 6 6 0 01-6-6 4 4 0 014-4 2 2 0 001.85-1.23z" /></svg>
      <h2>Play</h2>
      <p>Play NAM amp models. Add the sound of a speaker cab with a cabinet impulse response (cab IR).
      Or loop your guitar with Dreamcatcher's delay and reverb.</p>
      <span class="og-path__more">Choose firmware</span>
    </a>
    <a class="og-path" href="docs/launchpad/">
      <svg class="og-icon" viewBox="0 0 24 24" aria-hidden="true"><!-- Lucide "laptop", ISC licence --><path d="M18 5a2 2 0 0 1 2 2v8.526a2 2 0 0 0 .212.897l1.068 2.127a1 1 0 0 1-.9 1.45H3.62a1 1 0 0 1-.9-1.45l1.068-2.127A2 2 0 0 0 4 15.526V7a2 2 0 0 1 2-2z" /><path d="M20.054 15.987H3.946" /></svg>
      <h2>Manage</h2>
      <p>Use Launchpad to flash firmware, load sounds, edit presets and back up supported data.
      It runs on macOS, Windows and Linux.</p>
      <span class="og-path__more">Launchpad</span>
    </a>
    <a class="og-path" href="docs/platform/">
      <svg class="og-icon" viewBox="0 0 24 24" aria-hidden="true"><!-- Lucide "code-xml", ISC licence --><path d="m18 16 4-4-4-4" /><path d="m6 8-4 4 4 4" /><path d="m14.5 4-5 16" /></svg>
      <h2>Build</h2>
      <p>Write the sound, screen and controls in C. Test the effect on your computer, then flash it to the pedal.</p>
      <span class="og-path__more">Your first effect</span>
    </a>
  </div>
</section>

<section class="og-code">
  <div class="og-wrap og-code__grid">
    <div class="og-code__text">
      <h2>The tremolo's audio code</h2>
      <p>This loop makes the volume rise and fall. A full effect also needs code for settings and controls.
      The shared platform supplies tools for USB, the screen, storage and presets.
      Launchpad uses your settings list to show an editor on your computer.</p>
      <p><a href="docs/platform/first-effect/">Your first effect</a> builds it step by step.</p>
    </div>
<div class="og-code__sample">

```c
static void fw_process(const int32_t *in,
                       int32_t *out_left, int32_t *out_right)
{
    for (uint32_t i = 0; i < GP5_BLOCK; i++) {
        float wave = 0.5f + 0.5f * sinf(2.0f * GP5_PI * phase);
        float gain = 1.0f - depth * wave;
        phase += step;
        if (phase >= 1.0f) phase -= 1.0f;
        out_left[i] = out_right[i] = (int32_t)((float)in[i] * gain);
    }
}
```

</div>
  </div>
</section>

<section class="og-note">
  <div class="og-wrap">
    <p><strong>Before you flash:</strong> a known DSP update fault can block a USB flash and may need a debug adapter wired inside the pedal.
    Read <a href="docs/get-started/safety/">Before you flash</a> for the risks and backup steps.</p>
    <p class="og-note__small">Valeton does not make or endorse OpenGP5. "Valeton" and "GP-5" are its trademarks.
    We use those names to show which pedal runs this code. MIT licence.</p>
  </div>
</section>
