# USB input and bindings

USB1 accepts keyboards, mice, HID controllers and supported Xbox/PlayStation
controllers through a hub. The binding list identifies HID interfaces; a
composite device can have separate keyboard, mouse and controller interfaces.
An interface is bindable when it advertises joystick/gamepad controls or a
supported axis. Ordinary keyboard and consumer keys are not binding sources.

Supported axes are X, Y, Z, Rx, Ry, Rz, Slider, Dial, Wheel and horizontal Pan.
The descriptor determines whether an axis reports an absolute position or
relative movement. Buttons belonging to a bindable application are available
alongside its axes. Report IDs retain independent state, so a wheel report
does not release held buttons or repeat earlier mouse movement.

New mouse and other generic axis devices start with assignment Off and empty
bindings. Select a player and assign the desired controls before using them
as a controller. Their ordinary keyboard and IIgs mouse functions still work.
They do not acquire a player automatically or use gamepad button shortcuts
to navigate the menu. Existing gamepad profiles and their six axis tuning
records retain their saved format and binding numbers.

The tester shows ten axes and distinguishes directions reported as a D-pad
hat from those reported as X/Y. Many SNES-style pads use absolute X/Y for
their physical D-pad; those readings can be bound normally. Generic absolute
axes can be captured from their resting position without centering them.

IIgs joystick packets carry unsigned paddle coordinates: 0 is left/up, 128
is center and 255 is right/down. The IIgs adapter converts these to the
imported core's signed MiSTer axes. Shared controller Y increases upward;
the ARM IIgs input service reverses it for Apple paddle Y. M0.6.3 corrects
both conversions. Its centering correction requires the matching new IIgs
FPGA image as well as the ARM firmware. Host tests cover controller bindings
through packet encoding; HDL simulation covers the full axis range and
the Apple-visible paddle timer reads. Board acceptance is recorded separately.

Relative movement produces short centered velocity pulses when bound to an
analog control, and directional presses when bound to a digital control.
Mouse movement scales by 256 per reported count. Dial, wheel and pan detents
produce signed 4096 pulses with 20 ms pressed/released phases; bursts queue
up to 32 pending detents per axis. Pulses expire without another USB report,
and pending motion is discarded on menu/core transitions or disconnect.
The extra four axes use fixed unity gain; six-axis tuning and native IIgs
mouse sensitivity remain separate settings.

Report protocol preserves mouse wheels, extra buttons and packed 12/16-bit
coordinates. Boot-capable devices fall back to their basic boot format if
descriptor parsing or report-protocol selection fails. If boot selection
also fails, input is disabled until reconnect instead of decoding an unknown
format. Pure boot keyboards retain their boot keyboard path. Composite
report keyboards support arrays, modifiers and variable bitmap keys.

The explicit UART command `:hid` reads descriptors once for diagnostics.
Its control requests may briefly pause foreground work. It adds no periodic
logging or report-callback telemetry. Host fixtures include the seven
interfaces captured from the owner's hub under M0.6.2-hid1.

On 2026-10-16, the owner confirmed M0.6.2 on hardware: the five-button
mouse's buttons, movement and wheel, the keypad wheels, and the SNES-style
pad's X/Y direction indicators all registered in the Apple IIgs controller
tester. Host validation passed 93 CTest cases, 17 menu scenarios and parser
sanitizer checks. The update reuses the existing FPGA images.
