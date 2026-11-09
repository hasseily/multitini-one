Supply your ZX Spectrum ROM as spectrum.rom in this directory.

48K PAL needs a 16384-byte Spectrum 48K ROM.
128K PAL needs a 32768-byte ROM with the editor and 128K menu first,
followed by the 48K BASIC ROM. The file size selects the machine model.

Put your TAP and TZX images in ../media/. The core supports read-only images
up to 131072 bytes. F12 opens the Multitini menu. In Media, Tape speed
cycles Normal, Fast (8x), and Instant when the stock ROM is recognized.
Start the guest tape loader before pressing F1 to play in Normal or Fast.
Instant responds to the stock ROM loader automatically for TAP files.
TZX uses pulse playback. F2 rewinds.
