# First Hardware Test

Status: engineering diagnostic image built; no board result is claimed.

## What this image is

`build/firmware/FIRMWARE.BIN` contains the generated Zynq FSBL, the routed
default PAL C64 shell bitstream, and the UART diagnostic application. The
physical DVI and audio paths run the independent shell diagnostics. The C64
core remains in reset because no BASIC, KERNAL, or character ROM is embedded
and this diagnostic has no file-transfer command.

The 2026-08-29 image is 4,235,532 bytes. Its SHA-256 is recorded in
`build/firmware/SHA256SUMS.txt`. The Vivado build manifest identifies commit
`fd554dbf46e22514bb132f3dc063ee0a892bdcc6` and also records that the
engineering source tree was dirty. This is a first-board test image, not a
release image.

## Before programming

- Confirm the board has the required stand-alone 5 V jumper fix.
- Use a current-limited supply for the first power-up.
- Preserve the golden image at QSPI offset `0x00000000`.
- Program only the firmware slot at offset `0x00200000`. Its capacity is
  `0x00DF0000` bytes; this image fits with 10,378,996 bytes free.
- Start with the DAC and amplifier volume low. The image boots with audio
  muted.

Do not write `FIRMWARE.BIN` directly at flash offset zero.

For the least persistent first check of this exact shell, load the packaged
`multitini_one_top.bit` and `diagnostic.elf` over JTAG after running the
generated Zynq PS initialization. That leaves QSPI unchanged. The golden-UART
path below is the routine firmware-slot update after that check, or when the
golden recovery path is already trusted.

## Program through golden UART

The normal path uses the preserved Appletini golden updater. With the board at
the golden `[SER] cmd>` prompt on UART0 at 921600 baud, run from this repository:

```powershell
python ..\appletini-one\scripts\serial_firmware_update.py `
  .\build\firmware\FIRMWARE.BIN --port COM3 --no-wait-after
```

Replace `COM3` if needed. After the transfer completes, close the updater and
open UART0 at 115200 baud, 8 data bits, no parity, and 1 stop bit.

## Expected first observations

Known source behavior:

- DVI emits the enabled 1920x1080p60 diagnostic pattern.
- The UART prints `Multitini One diagnostic firmware` and a shell-probe result.
- A successful shell probe is followed by TMP102, muted DAC initialization,
  register status, and the command list.
- Apple-bus, Ethernet, and PSRAM control pins remain at their stand-alone idle
  values; PSRAM is not enabled.

Record actual behavior separately. A build result does not prove any item on
the board.

Useful UART commands are:

| Command | Action |
| --- | --- |
| `s` | Print shell status and build ID |
| `p` | Exercise the command mailbox |
| `v` | Toggle the DVI diagnostic pattern |
| `a` | Unmute the diagnostic audio tone |
| `m` | Mute audio |
| `r` | Toggle C64 core reset request; it does not load ROMs |
| `:reboot` then Enter | Return to the golden image |

Only use `a` after confirming the DAC initialized and the downstream audio
level is safe.

## Return to golden and update again

The diagnostic UART runs at 115200 baud while golden runs at 921600 baud. Do
not use the updater's one-baud `--reboot-golden` flow across this transition.
Instead:

1. At 115200 baud, send `:reboot` followed by Enter.
2. Close the terminal.
3. Run the golden updater command above at its default 921600 baud.

The recovery-only console also accepts `:reboot` if the FPGA shell probe fails.

## Record the result

Capture the board revision, jumper state, supply voltage/current, UART log,
display make/mode, TMP102 reading, DVI behavior, audio behavior, and whether
golden recovery succeeded. Do not call the C64 functional until rights-cleared
ROM loading, machine video/audio selection, and on-board boot testing pass.
