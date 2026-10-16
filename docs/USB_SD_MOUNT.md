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

Complete USB writes reach the SD backend and finish synchronization before a
successful SCSI write acknowledgement. Stopping gives an incomplete transfer
two seconds to finish, then detaches USB and cancels that incomplete command.
An already running polled SD operation can extend the wait by its driver
timeout. Complete queued writes are flushed before canceling class state.
Both USB endpoint DMA and SD DMA must be quiescent before local filesystem
access resumes. A failed SD operation invalidates the shared driver's ready
state and requires a controller reset before buffers can be reused. A reset
card is initialized again by the local remount. DMA or sync failures retain
exclusive ownership and display a retry status; so does a failed remount.
Interrupted transfers are reported after exit: check the copied files.

After a successful remount, Multitini rescans packages and settings and
restarts the previously active core from the current files on SD. Unsaved
machine state is lost. If that package was removed or cannot restart, the
machine stays in reset and the menu allows another core to be chosen.

## Recovery and diagnostics

M0.5.9 removes interrupt-handler infinite loops after failed USB descriptor
sends. A mass-storage reset clears deferred DATA and rebuilds the bulk
endpoint rings only after their DMA stops. Invalid command wrappers, failed
receive descriptors and dropped queue packets stall bulk traffic until host
reset recovery; they cannot become a new disk command or silently shift a
write's payload. Endpoint flush waits are bounded.

During USB SD mode, UART `u` prints a short snapshot of existing transfer,
reset and error counters. It adds no periodic logging or new per-packet
timestamps. **Printing can briefly pause foreground transfer service**; use
it after a failure, not during a throughput measurement. Another snapshot is
printed after detach. This temporary report should be removed once it has
served the intermittent-failure diagnosis.

On 2026-10-16 the owner tried one roughly 700 MB copy at about 8 MB/s, a group
of about twelve files, and individual copies without reproducing the freeze.
Passive UART captures on COM12 and COM13 received no bytes. These trials did
not establish the installed firmware identity or validate M0.5.9. The original
intermittent trigger remains unobserved.

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
original USB integration passed the then-current 84-test GNU host suite.
The recovery regressions additionally inject descriptor-send failures,
malformed command wrappers, stale DATA at mass-storage reset, endpoint flush
failures, queue overflow and abandoned-write deadlines. SD recovery tests
exercise the generated driver patch and controller quiescence separately
from the modal lifecycle. None of these tests reproduce a physical USB fault.
M0.5.9 passes all 92 GNU host tests, strict ARM/BSP object compilation and
the complete Vitis firmware build. Its eight FPGA images are reused exactly;
the rebuilt video worker differs only in its GNU build-ID metadata. The new
firmware has not been installed or tested on the board.

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
5. In a controlled fault test, abandon a host write, then request stop. Check
   that the incomplete command is canceled after its grace period, both DMA
   engines stop and local access resumes only after remount. Injected DMA or
   sync failures must keep local access blocked until recovery. Verify the
   interruption notice and recheck copied file contents.

The device transport derives from Appletini One commit
`7b972bfda432657444d55a3eaaccf4b1d8e06699`. Original and ported file hashes,
licenses and adaptations are recorded in `provenance/usb_storage.import.json`
and `licenses/files.csv`. USB1 host behavior and Golden firmware are retained.
