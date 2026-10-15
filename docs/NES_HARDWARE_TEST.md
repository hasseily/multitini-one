# NES hardware test

Install the complete `build/firmware-core-nes/sd/` contents, preserving existing
media and settings. Update Multitini through Golden with the included
`FIRMWARE.BIN`, then select **Nintendo Entertainment System** in Cores.
Use the package's `SHA256SUMS.txt` to identify the installed files.
This branch has no recorded NES board acceptance.

## Cartridges and controls

Put user-owned `.nes` files in `multitini/cores/nes/media/`. No NES BIOS is
needed. The package includes no NES cartridges or Nintendo firmware.
The initial port accepts standard NTSC iNES 1 cartridges with mapper IDs
0, 1, 2, 3, 4, 7, 9, 10, 11, 66 and 71, subject to the parser's size limits.
See [NES integration](NES_PLAN.md) for exclusions and architecture.

Two standard controllers use the shared controller setup and player
assignments. Keyboard player 1 uses arrows, Z for A, X for B, Enter for Start
and Right Shift for Select. F12 opens the shared menu; the common gamepad
Select+Start gesture also opens it.

Battery SRAM is limited to 8 KiB. Saves live beside the cartridge as
`<cartridge>.nes.sav`, with the shared recovery journal. Complete an eject or
core switch before removing power. Preserve journals if recovery is needed.

## Board checks

1. Cold boot, select NES, browse nested folders and load a supported cartridge.
   Check reset, menu pause/resume and loading another cartridge. Unsupported
   headers or mapper IDs should report an error and leave the menu usable.
2. Check both serial controllers, keyboard controls and remapping. Enter the
   menu while buttons are held, disconnect and reconnect controllers, and
   confirm that released buttons do not remain pressed. Reassign player 2.
3. Check full 256x240 video, including both edge columns and the bottom row.
   Exercise all advertised output modes, Auto and manual sizes, cropping,
   screenshots and the shared picture effects. Native NTSC and output refresh
   differ slightly; the common frame conversion may repeat or drop a field.
4. Check APU pulse, triangle, noise and DMC audio, including sustained gameplay.
   Expansion audio and peripherals outside the implemented mapper set have
   no acceptance claim. Check timing with changing graphics and busy audio.
5. Use user-owned cartridges from each implemented mapper family. Check bank
   switching, mirroring and mapper IRQs where used. Record exact cartridge
   hashes and results; one title cannot establish an entire mapper's coverage.
6. For a battery cartridge, change saved data, eject, reload and power cycle.
   Confirm persistence. Switch from a battery cartridge to a nonbattery
   cartridge and back. Test interrupted-write recovery with disposable saves.
7. Switch between NES and every installed core. Change output modes before
   and after switching. Confirm that pending save and video transfers finish
   and that the next core starts normally.
8. Use System USB SD mount after changing battery SRAM. Confirm that saves
   finish before the PC sees the card. Safely eject on the PC, close the modal
   and reload content. Also check USB disconnect and failed-remount recovery.

Record the package SHA-256, board identity, cartridge hashes, controller
models, output mode and results. Source tests, simulation and routed timing
remain separate evidence from physical behavior.
