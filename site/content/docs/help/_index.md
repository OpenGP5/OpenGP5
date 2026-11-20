---
title: Help
description: Questions, problems, reporting how your pedal did, and contributing.
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
| A flash failed, or the pedal doesn't start | [Recovery](../get-started/recovery/) |
| Launchpad says MIDI access isn't available | Click **Restart Launchpad** in its banner ([Connect](../launchpad/#connect)) |
| The browser can't find the pedal | Use Chrome, Edge or Opera on a computer, and allow MIDI access ([web flasher](../get-started/flash/#in-your-browser)) |
| My presets or captures are gone after flashing | Switching firmwares wipes the old one's data; restore a [backup](../launchpad/backups/) |
| I want Valeton's firmware back | [Going back](../get-started/recovery/#going-back-to-valetons-firmware) |
| My own firmware misbehaves | [Debugging](../platform/debugging/) |

Common questions are answered in the [FAQ](../platform/faq/), and what OpenGP5 can't do yet is in
[Known limits](../platform/known-limits/).

## Report a problem

Open an issue on GitHub, in the repository it belongs to:

- [OpenGP5](https://github.com/OpenGP5/OpenGP5/issues): the platform, flashing, the web flasher, recovery
- [Launchpad](https://github.com/OpenGP5/Launchpad/issues)
- [NAM Loader](https://github.com/OpenGP5/NAM-Loader/issues), [Dreamcatcher](https://github.com/OpenGP5/Dreamcatcher/issues)

Say what you did, what you expected, and what happened, with the firmware and Launchpad versions.

## Help the project

- **[Beta testing](beta-testing/):** an hour's checklist on your pedal. Every pedal's report helps, whether
  everything worked or not.
- **[Contributing](contributing/):** fixes, firmwares, docs.
