# Contributing to OpenGP5

OpenGP5 is a small project that a few people work on in their spare time.
Help by reporting clear steps, making focused changes or improving the docs.

## Ways to help

- **Try it on your pedal and tell us how it went.** OpenGP5 has run on only a handful of GP-5s so far. The
  [beta-testing checklist](BETA-TESTING.md) takes about an hour. A
  [hardware report](https://github.com/OpenGP5/OpenGP5/issues/new?template=hardware.yml) can describe passed checks as well as faults.
- **Write a firmware.** Start from the [firmware template](https://github.com/OpenGP5/firmware-template), then
  [list it in the catalog](catalog/README.md) so Launchpad and the web flasher can offer it.
- **Report a bug** with the [bug form](https://github.com/OpenGP5/OpenGP5/issues/new?template=bug.yml).
- **Improve the platform or the docs** with a pull request (below).
- **Ask and answer questions** in [Discussions](https://github.com/OpenGP5/OpenGP5/discussions).

## Before you start on a change

For a change beyond a fix or doc edit, open an issue or discussion. Describe what you want to change and why. The docs target platform 0.8.0-alpha and the planned 1.0 API.
Discuss changes to public headers, callbacks, manifest fields or USB commands before writing them.

## Pull requests

1. Fork the repository, create a branch, and make one focused change.
2. Run the tests on your computer. They need no pedal and no cross-compilers:
   ```
   tools/test_all.sh
   ```
   If you changed platform code, also build the examples for the pedal (`release/setup-toolchain.sh` once, then
   `platform/build_firmware.sh tremolo`). Continuous integration (CI) runs these checks on each pull request.
3. Anything that changes how the pedal behaves needs a test on a real pedal before it's merged. Say in the pull
   request whether you did one, and how: the maintainers may do it if you can't.
4. Keep pull requests to one change each, and describe what and why.

### How the code is written

- C99, no C library on either processor: the platform provides what's needed (`gp5_math.h`, `gp5_fmt.h`).
- Nothing from the chip vendor's SDK and nothing from Valeton: no code, headers, libraries, firmware or data. Code
  here is written from how the hardware behaves. A pull request that includes any of it can't be merged.
- Comments explain why the code works this way. Use plain words and match the file's style.
- For each new public call or callback, add a header comment and an entry in `docs/platform/api.md`.
- New host tests go into `tools/test_all.sh`.

### Docs

Write for someone who has not worked with the pedal's hardware.

- Say what a tool does before explaining how it works.
- Use short sentences, active voice and plain words.
- Aim for a Flesch Reading Ease score of 80 or higher in prose.
- Remove adverbs and vague claims about ease, speed or safety.
- Keep a calm tone. Avoid sales language and buzzwords.
- Use technical terms when they help, and define them at first use.
- Put code names in `backticks`. Match button labels to the app.
- State whether an edit changes the live sound, saved settings or a preset.
- Keep version requirements and recovery limits consistent across guides.

## Licence

OpenGP5 uses the MIT licence. If you send a pull request, you agree to share your work under that licence.

## Conduct

Be kind and assume good intent: see the [code of conduct](CODE_OF_CONDUCT.md).
