---
title: Help
description: Find a fix, report a problem, or help improve OpenGP5.
weight: 50
cascade:
  - target: {path: /docs/help/changelog}
    title: Changelog
    weight: 40
    repo_path: CHANGELOG.md
    description: What changed in each release of OpenGP5, and how to update a firmware.
  - target: {path: /docs/help/beta-testing}
    title: Beta testing
    weight: 20
    repo_path: BETA-TESTING.md
    description: Try OpenGP5 on your pedal with a short checklist, and report how it went.
  - target: {path: /docs/help/contributing}
    title: Contributing
    weight: 30
    repo_path: CONTRIBUTING.md
    description: Reporting bugs, pull requests, and how the code is written.
  - target: {path: /docs/help/code-of-conduct}
    title: Code of conduct
    weight: 50
    repo_path: CODE_OF_CONDUCT.md
---

## Something went wrong

| Problem | Where to look |
|---|---|
| A flash failed, or the pedal does not start | [Recovery](../get-started/recovery/) |
| Launchpad cannot access MIDI | Click **Restart Launchpad** in its banner; see [Connect](../launchpad/#connect) |
| The browser cannot find the pedal | Use Chrome, Edge or Opera on a computer and allow MIDI access; see [Web flasher](../get-started/flash/#in-your-browser) |
| Presets or captures disappeared after a flash | A different firmware starts with empty storage; reinstall the matching firmware and [restore its backup](../launchpad/backups/) |
| I want Valeton's firmware back | [Going back](../get-started/recovery/#going-back-to-valetons-firmware) |
| My own effect has a fault | [Debugging](../platform/debugging/) |

Read the [FAQ](../platform/faq/#using-it) for common questions.
Check [Known limits](../platform/known-limits/) for faults and the limits of the pedal and tools.

## Report a problem

Open a GitHub issue in the project that has the fault:

- [OpenGP5](https://github.com/OpenGP5/OpenGP5/issues): the platform, flash tools, web flasher and recovery.
- [Launchpad](https://github.com/OpenGP5/Launchpad/issues): the desktop app.
- [NAM Loader](https://github.com/OpenGP5/NAM-Loader/issues) or [Dreamcatcher](https://github.com/OpenGP5/Dreamcatcher/issues): the effect.

Give enough detail for someone to repeat the fault:

- what you did, in order;
- what you expected and what happened;
- the effect name and version;
- the version of the shared platform code, if known;
- the app or flash-tool version and your system, such as macOS or Windows;
- the error text or log output.

The effect and shared code have their own version numbers. Do not assume they match.
Launchpad's Home page shows what the pedal runs.

## Help the project

- Follow [Beta testing](beta-testing/) and report which checks passed or failed. Allow about an hour for the full list.
- Read [Contributing](contributing/) to fix code, edit the docs or share an effect.
