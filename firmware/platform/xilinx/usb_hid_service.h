/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_USB_HID_SERVICE_H
#define MULTITINI_USB_HID_SERVICE_H

#include "multitini_core_link.h"
#include "multitini_gamepad.h"
#include "multitini_controls.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t reports;
    uint32_t submit_errors;
    uint32_t transfer_errors;
    uint32_t input_errors;
    uint32_t portsc;
    int last_error;
    uint8_t started;
    uint8_t keyboards;
    uint8_t gamepads;
    uint8_t joystick_port;
    uint8_t symbolic_keyboard;
} mt_usb_hid_status_t;

/* Return true to capture this complete boot-keyboard report for the
 * supervisor UI instead of forwarding it to the active machine. */
typedef bool (*mt_usb_keyboard_filter_fn)(void *context, uint8_t slot,
                                          uint8_t modifiers,
                                          const uint8_t *keys,
                                          size_t key_count);

void usb_hid_service_init(mt_core_link_t *link);
int usb_hid_service_start(void);
int usb_hid_service_stop(void);
void usb_hid_service_poll(void);
/* Collect reports and run the keyboard filter without issuing core commands.
 * Pending input changes are delivered by the next ordinary poll. */
void usb_hid_service_poll_deferred(void);
/* Normal and deferred full polls share a deadline measured from completion.
 * A zero interval always polls; this does not throttle ordinary runtime polls. */
void usb_hid_service_poll_deferred_if_due(uint32_t min_interval_us);
void usb_hid_service_vectrex_controls(int controls[2][6]);
void usb_hid_service_set_active_core(uint32_t core_id);
void usb_hid_service_set_core_ready(bool ready);
void usb_hid_service_set_keyboard_filter(mt_usb_keyboard_filter_fn filter,
                                         void *context);
int usb_hid_service_set_joystick_port(uint8_t port);
uint8_t usb_hid_service_joystick_port(void);
int usb_hid_service_set_symbolic_keyboard(bool enabled);
bool usb_hid_service_symbolic_keyboard(void);
void usb_hid_service_get_status(mt_usb_hid_status_t *status);
void usb_hid_service_dump_status(void);
/* Explicit foreground diagnostic only; performs bounded descriptor requests.
 * No logging or extra work is added to normal report/poll callbacks. */
void usb_hid_service_dump_descriptors(void);

void usb_hid_service_set_menu_capture(bool captured);
bool usb_hid_service_set_iigs_mouse_sensitivity(uint16_t percent);
void usb_hid_service_set_gamepad_preview(bool preview);
void usb_hid_service_gamepad_snapshot(uint8_t slot, mt_gamepad_state_t *raw,
                                      mt_gamepad_state_t *mapped);
bool usb_hid_service_set_gamepad_config(uint8_t slot, const mt_gamepad_config_t *config);
void usb_hid_service_get_gamepad_config(uint8_t slot, mt_gamepad_config_t *config);

typedef bool (*mt_usb_controller_config_fn)(void *context, uint32_t core_id,
    uint8_t slot, const mt_controller_device_t *device, mt_controller_config_t *config);
void usb_hid_service_set_controller_config_loader(mt_usb_controller_config_fn loader,
                                                  void *context);
bool usb_hid_service_controller_device(uint8_t slot, mt_controller_device_t *device);
bool usb_hid_service_get_controller_config(uint8_t slot, mt_controller_config_t *config);
bool usb_hid_service_set_controller_config(uint8_t slot, const mt_controller_config_t *config);
bool usb_hid_service_validate_controller_configs(const mt_controller_config_t configs[MT_GAMEPAD_SLOTS]);
bool usb_hid_service_set_controller_configs(const mt_controller_config_t configs[MT_GAMEPAD_SLOTS]);
void usb_hid_service_controller_snapshot(uint8_t slot, mt_gamepad_state_t *raw,
                                         mt_controls_output_t *output);
/* Suppress report and polling based assignment while the escape pair is held. */
bool usb_hid_service_controller_menu_chord(uint8_t slot);
/* Zero/one are machine ports 1/2; 255 means connected but unassigned. */
uint8_t usb_hid_service_controller_player(uint8_t slot);
typedef void (*mt_usb_controller_action_fn)(void *context, unsigned action);
void usb_hid_service_set_controller_action_handler(mt_usb_controller_action_fn handler,
                                                    void *context);
typedef void (*mt_usb_controller_report_fn)(void *context, uint8_t slot,
                                            const mt_gamepad_state_t *raw);
void usb_hid_service_set_controller_report_handler(mt_usb_controller_report_fn handler,
                                                    void *context);

#endif
