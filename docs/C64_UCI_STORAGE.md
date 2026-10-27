# C64 Command Interface storage

The Ultimate-compatible Command Interface exposes a virtual device directory
at `/`. `OPEN_DIR` followed by `READ_DIR` returns `SD` and `Temp`, each with
the directory attribute `0x10`. Programs such as UBoot64 discover the SD
device through this listing.

`/SD` contains the configured C64 media directory on the physical SD card.
It does not expose the rest of the card. `/Temp` uses the existing temporary
directory and cleanup policy. `/Usb0`, `/Usb1` and `/Usb2` remain aliases for
the same SD media; they are not advertised as additional physical devices.

Each DOS instance keeps its own current directory. `GET_PATH` returns a
canonical path under `/SD/` or `/Temp/`, or `/` at the device directory.
Changing from `/SD` or `/Temp` to `..` returns to `/`. Initial state, reset
and `COPY_HOME` select the physical media or configured home directory,
so existing relative file access continues to work. Legacy absolute paths
such as `/games/demo.prg` also remain relative to the confined media root.

The host regressions cover discovery on an empty media directory, actual
media listing, directory attributes, snapshot replay and abort, independent
DOS instances, aliases, parent navigation, Temp writes, home/reset behavior,
protected paths and virtual disk images. They also check that REU commands
use the FPGA's effective expansion state when cartridge coexistence is
advertised, while older images retain their existing configuration check.

These are source and host-test results. UBoot64 discovery and updater use
still require validation on the installed ARM/FPGA package.
