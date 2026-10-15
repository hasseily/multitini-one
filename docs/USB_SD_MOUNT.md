# USB SD card access

In **System**, select **USB SD card**. Its value is **Mount on PC; restart
core on exit**. Connect the PC to the board's **USB0 device connection**.
USB1 remains the host connection for the keyboard and controllers.

The PC receives a USB mass-storage device containing the SD card's physical
sectors, including its partition table. It can access the existing partitions
and files. This is exclusive access to the card; it is not a shared network
folder or a live mount of an emulated disk image.

Before connecting the USB device, Multitini saves and ejects core media,
closes documents, cancels screenshots, resets the active core and drains its
memory/video transfers. FatFs then unregisters the card. Export is refused if
media cannot flush or a file remains open. Local SD clients cannot remount or
write the card during export. Core switching, firmware updates and ordinary
menu actions are unavailable while this modal screen is open.

Copy the desired files using the PC. Use its **Eject / Safely Remove** action
before stopping export. A completed host eject can close the screen
automatically. Otherwise press **Enter**, **Esc** or **F12** on the Multitini
keyboard. The serial console also accepts Enter, Esc or `q` to stop.

Complete USB writes reach the SD backend and finish `CTRL_SYNC` before a
successful SCSI write acknowledgement. An incomplete transfer delays stopping.
The controller must stop, its endpoint DMA must drain, and SD synchronization
must succeed before local filesystem access resumes. Sync or DMA errors keep
the card exclusive and display a retry status. A failed local remount also
retains exclusive ownership and retries while the screen remains open.

After a successful remount, Multitini rescans packages and settings and
restarts the previously active core from the current files on SD. Unsaved
machine state is lost. If that package was removed or cannot restart, the
machine stays in reset and the menu allows another core to be chosen.

## Validation and limits

The host tests exercise the actual SD ownership layer and backend, the modal
lifecycle, and the production SCSI final-write handler. They cover open files
and in-progress callbacks, flush refusal, connection failure, pending writes,
write/sync failures, host eject, failed remount, physical-sector bounds, IRQ
rejection and card recovery. System-menu tests cover the new action and the
existing forms. A queue regression injects resets during blocking SD work,
checks nested USB0 interrupt ownership and rejects the original stale-request
replay. Write-handler tests suppress obsolete replies after bus, port and
mass-storage resets without consuming pending endpoint-zero requests. The
complete GNU host suite passed with 84 tests.

`scripts/test_usb_storage_native.py --bsp <generated-domain>` compiles the
production USB0 sources against the actual ARM compiler and Xilinx BSP.
The full Vitis build separately checks the linked firmware and embedded CPU1
image. Neither check establishes PC enumeration, throughput or safe-eject
behavior on a physical board. Hardware USB testing remains required.

For the first board test, use a backed-up test card:

1. Run a core with writable media, then open **System → USB SD card**.
   Check that core activity stops, the keyboard still responds and the PC
   sees the expected card capacity and partitions.
2. Copy a test file onto the card, read it back and compare its checksum.
   Rename and remove that test file through the PC filesystem.
3. Eject the device through the PC. Check that the modal exits, the active
   core restarts and newly copied media appears in its browser.
4. Repeat export and stop with **F12** after PC transfers have completed.
   Check that the core restarts and local saves still work.
5. In a controlled fault test, keep a host write incomplete or inject an SD
   sync/remount failure. Check that stopping remains pending, local SD access
   stays blocked and recovery succeeds before the core restarts. The host
   fixtures cover these cases before board fault testing.

The device transport derives from Appletini One commit
`7b972bfda432657444d55a3eaaccf4b1d8e06699`. Original and ported file hashes,
licenses and adaptations are recorded in `provenance/usb_storage.import.json`
and `licenses/files.csv`. USB1 host behavior and Golden firmware are retained.
