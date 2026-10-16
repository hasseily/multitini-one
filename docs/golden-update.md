# Golden recovery images

On branch `multiboot`, use [the dual firmware layout and migration guide](MULTIBOOT.md).
Golden B2.0.0 validates both firmware slots and retains the primary/trial recovery
procedure. The single-slot layout below records the original port.

This ports Appletini ONE `be2ea4fee3c5ebc239b8b20aad9738bee89f8ff0`.
The serial protocol, manifest magic, recovery marker and flash metadata remain
compatible. Build labels identify the Multitini application. Board testing is
still required; no flash contents were changed during this port.

The 16 MiB QSPI layout is unchanged:

| Region | Offset | Capacity |
| --- | --- | --- |
| Primary golden A | `0x000000` | 1 MiB |
| Trial golden B | `0x100000` | 1 MiB |
| Firmware fallback | `0x200000` | `0xDF0000` bytes |
| Firmware metadata | `0xFF0000` | 64 KiB |

The Vitis workspace builds `golden/build/golden.elf` as well as the supervisor
and CPU1 worker. Package explicit artifacts with:

```
scripts\make_boot_bin.bat BOOT.BIN fsbl.elf golden.elf
scripts\make_firmware_bin.bat FIRMWARE-MULTITINI.BIN fsbl.elf supervisor.bit diagnostic.elf
```

Both packagers append and verify a 32-byte CRC manifest, enforce the destination
slot size, require the linked recovery marker, and install output only after
verification. The firmware bitstream must carry a `SUPERVISOR` kind marker.
Golden contains the FSBL and recovery app; it needs no configured fabric.

New Multitini deliveries use `FIRMWARE-MULTITINI.BIN` at both package root and
`sd/`. Copy it to the SD root. Multiboot Golden already scans this name and
renames a successful update to `FIRMWARE-MULTITINI.OK`; no Golden rebuild is
needed. Its legacy `FIRMWARE.BIN` input remains available for older packages.

Install recovery-capable firmware before trying to update golden. The updater
refuses to erase a slot unless primary is bootable and the firmware fallback
has a valid Bootgen image, matching CRC, firmware manifest and recovery marker.
An older fallback without this manifest must first be updated through the
existing golden firmware update path.

Place the packaged golden image at `0:/BOOT.BIN`. At the supervisor serial
console send `:selfupdate`. The command also works in the early recovery console
when probing the FPGA shell fails. It flushes runtime media, stops USB, checks
that SD owners are closed, stages B, verifies it, and selects B for reset only
on success. A failed update reports the error and retains the source file.
Nothing automatically starts a golden update merely because BOOT.BIN exists.

When B boots, it validates B and the fallback again before repairing A. A's
boot identifier is invalidated before erasing. Both installations withhold the
XLNX identifier until every image byte has passed readback, then verify again.
B remains intact throughout repair. The next reset selects primary. Interrupted
operations can be retried from the surviving copy. CRCs detect corruption;
these manifests do not authenticate image publishers.

The golden serial monitor retains its one-second startup window and commands
`selfupdate` (SD), `selfrx <size> <crc32>` (RAM upload), `rx <size> <crc32>`
(firmware upload), and `boot`. The host tool is
`scripts/serial_firmware_update.py`; run `--help` for port, reset-capture and
image-role options. Golden upload is bounded to one MiB and fully received and
checked before flash is touched. It does not require an SD card.

Native tests run the production C self-update engine against NOR erase-sector
and page-program fixtures. They cover power loss after every mutation, read and
write failures, corruption, missing/invalid fallback and target manifests,
marker boundaries, file-read failure, verified-image close failure, and retries.
The supervisor reboot tests verify media flushing, UART drain, multiboot
readback, USB shutdown, and recovery without uninitialized PL services.
Vivado/Vitis and physical reset/BootROM recovery remain board acceptance gates.
