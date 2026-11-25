# Back up a stock GP-50

Connect the GP-50 over USB in normal stock firmware mode and close Valeton Suite.
Install the Python MIDI packages used by `gp5tool.py`, then run:

```sh
python3 tools/gp50_backup.py ~/gp50-backup
```

The tool has been tested with stock V1.0.5. It exports 100 saved presets as
`.prst` files and reads them a second time to verify them. The GP-50's direct
export command leaves the current preset selected. Save any edits on the pedal
first if you want the edits included in the saved preset files.

`Backup.json` records the preset order, file checksums, and the names and slots
of user IRs and SnapTones. Keep the original WAV and NAM files for those slots.
`globals.bin` holds the raw settings reply for reference.

For preset restoration on a running pedal, import the `.prst` files into the
matching slots with Valeton Suite. If needed, reinstall the original IRs and
SnapTones into the slots in `Backup.json`. Keep a separate copy of Valeton's
[GP-50 V1.0.5 firmware](https://www.valeton.net/wpfd_file/gp-50-firmware-v1-0-5-zip%EF%BC%88compatible-with-computer-software-v1-0-9-and-mobile-software-v1-1-1%EF%BC%89/).

The bootloader and the exact contents of user IR and SnapTone slots require a
separate backup. GP-50 bootloader recovery and OpenGP5 firmware on a GP-50
still need hardware validation.
