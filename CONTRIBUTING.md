# Contributing to OpenGP5

Thanks for wanting to help. OpenGP5 is a small project run by a few people in their spare time, so clear issues and
small pull requests go furthest.

## Ways to help

- **Try it on your pedal and tell us how it went.** OpenGP5 has run on only a handful of GP-5s so far. The
  [beta-testing checklist](BETA-TESTING.md) takes about an hour. A
  [hardware report](https://github.com/OpenGP5/OpenGP5/issues/new?template=hardware.yml) helps the most, whether your pedal worked or not.
- **Write a firmware.** Start from the [firmware template](https://github.com/OpenGP5/firmware-template), then
  [list it in the catalog](catalog/README.md) so Launchpad and the web flasher can offer it.
- **Report a bug** with the [bug form](https://github.com/OpenGP5/OpenGP5/issues/new?template=bug.yml).
- **Improve the platform or the docs** with a pull request (below).
- **Ask and answer questions** in [Discussions](https://github.com/OpenGP5/OpenGP5/discussions).

## Before you start on a change

For anything bigger than a fix or a doc improvement, open an issue or a discussion first and describe what you have
in mind. The platform is close to its 1.0 API, so changes to what firmwares see (headers, callbacks, the manifest,
USB commands) need agreeing before they're written.

## Pull requests

1. Fork, branch, change.
2. Run the tests on your computer. They need no pedal and no cross-compilers:
   ```
   tools/test_all.sh
   ```
   If you changed platform code, also build the examples for the pedal (`release/setup-toolchain.sh` once, then
   `platform/build_firmware.sh tremolo`). CI does both on every pull request.
3. Anything that changes how the pedal behaves needs a test on a real pedal before it's merged. Say in the pull
   request whether you did one, and how: the maintainers may do it if you can't.
4. Keep pull requests to one change each, and describe what and why.

### How the code is written

- C99, no C library on either processor: the platform provides what's needed (`gp5_math.h`, `gp5_fmt.h`).
- Nothing from the chip vendor's SDK and nothing from Valeton: no code, headers, libraries, firmware or data. Code
  here is written from how the hardware behaves. A pull request that includes any of it can't be merged.
- Comments say why, in plain words. Match the style of the file you're in.
- A new public function or callback goes into its header with a comment and into `docs/platform/api.md`.
- New host tests go into `tools/test_all.sh`.

### Docs

Short sentences, plain words, written for someone who has never seen the pedal's insides. Say what something does
before how. Code names go in `backticks`.

## Licence

OpenGP5 is MIT-licensed. By sending a pull request you agree that your contribution is released under the same
licence.

## Conduct

Be kind and assume good intent: see the [code of conduct](CODE_OF_CONDUCT.md).
