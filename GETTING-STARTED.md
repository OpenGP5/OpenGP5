# Getting started

The guide is on the website now: **[Get started](https://opengp5.github.io/docs/get-started/)**.

- [Before you flash](https://opengp5.github.io/docs/get-started/safety/): what can go wrong, and what to back up
- [Flash a firmware](https://opengp5.github.io/docs/get-started/flash/): with Launchpad, in your browser, or from the command line
- [Recovery and going back to Valeton's firmware](https://opengp5.github.io/docs/get-started/recovery/)
- [Set up your computer](https://opengp5.github.io/docs/platform/setup/) to build your own

## Recovery

If a flash fails or the pedal doesn't start: unplug it, hold the **foot switch** and press the **PARA knob** in,
and plug it back in while holding both. The screen stays dark: that's update mode. Flash again, with Launchpad, the
[web flasher](https://opengp5.github.io/flasher/) or `python3 tools/gp5tool.py flash <file> --sections all`.
More, including when USB isn't enough: [Recovery](https://opengp5.github.io/docs/get-started/recovery/).
