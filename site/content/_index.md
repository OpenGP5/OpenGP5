---
title: OpenGP5
description: Open firmware for the Valeton GP-5 guitar pedal, and a framework for writing your own.
---

<section class="og-hero">
  <div class="og-hero__inner">
    <div class="og-hero__text">
      <h1 class="og-hero__title"><img src="wordmark.png" alt="OpenGP5" width="740" height="159"></h1>
      <p class="og-hero__lead">Open firmware for the Valeton GP-5 guitar pedal, written from scratch. Play NAM
      captures, build ambient textures, or write your own effect in C.</p>
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
      <svg class="og-icon" viewBox="0 0 24 24" aria-hidden="true"><g transform="rotate(32 12 12)">
        <path d="M11 13.6c-.9-1.8-1.6-3.4-2.5-3.1-1.1.4-.5 3.4-1.4 5.1-.9 1.5-1.9 2.8-1.3 4.7.7 2 3.7 2.7 6.2 2.5 2.5.2 5.5-.6 6.2-2.6.6-1.8-.6-3-1-4.2-.4-1.4 0-3.4-1.2-4-1-.5-1.8.9-3 1.6"/>
        <path d="M11 13.6V3.8M13 13.6V3.8M11 3.8l-.4-2.6 3-.4-.6 3"/>
        <path d="M10.3 17h3.4M10.3 19.2h3.4"/></g></svg>
      <h2>Play</h2>
      <p>NAM captures with their own cab IRs, or Dreamcatcher's looper, glitch delay and reverb. Each firmware turns
      the pedal into something new.</p>
      <span class="og-path__more">The firmwares</span>
    </a>
    <a class="og-path" href="docs/launchpad/">
      <svg class="og-icon" viewBox="0 0 24 24" aria-hidden="true">
        <rect x="4" y="5" width="16" height="11" rx="1.5"/><path d="M2 19h20"/><path d="M9 10.5l2 2 4-4"/></svg>
      <h2>Manage</h2>
      <p>Launchpad flashes firmware, loads your captures and IRs, edits presets while you play, and backs everything
      up. On macOS, Windows and Linux.</p>
      <span class="og-path__more">Launchpad</span>
    </a>
    <a class="og-path" href="docs/platform/">
      <svg class="og-icon" viewBox="0 0 24 24" aria-hidden="true">
        <path d="M8 8l-4 4 4 4M16 8l4 4-4 4M13.5 5l-3 14"/></svg>
      <h2>Build</h2>
      <p>Write an effect in plain C: your sound, your screen, your controls. Hear it on your computer before it goes
      on the pedal.</p>
      <span class="og-path__more">Your first effect</span>
    </a>
  </div>
</section>

<section class="og-code">
  <div class="og-wrap og-code__grid">
    <div class="og-code__text">
      <h2>An effect is a few lines of C</h2>
      <p>This is the whole sound of the tremolo, the example firmware. The platform handles the rest: both
      processors, USB, the screen, the knobs and foot switch, saving settings and presets, and an editor for them
      on your computer.</p>
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
    <p><strong>OpenGP5 is in alpha.</strong> It has run on several GP-5s with no problem reported, and a bad flash
    can almost always be undone over USB. <a href="docs/get-started/safety/">Before you flash</a> explains the rare
    exception.</p>
    <p class="og-note__small">Not affiliated with or endorsed by Valeton. "Valeton" and "GP-5" are Valeton's
    trademarks, used only to say which pedal this runs on. MIT licence.</p>
  </div>
</section>
