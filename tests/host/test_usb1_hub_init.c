/* SPDX-License-Identifier: GPL-3.0-only */
/* Exercise the production hub allocation/cleanup path without a controller. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(v) do { if (!(v)) { \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #v); exit(1); \
} } while (0)
#define CONFIG_USBHOST_MAX_RHPORTS 1U
#define USB_LOG_ERR(...) ((void)0)
#define USB_HUB_INDEX_ANY 0U
#define USB_HUB_PORT_ANY 0U
#define USB_INTERFACE_ANY 0U
#define USBH_EVENT_INIT 1U

struct usbh_bus;
struct usbh_hub {
    bool connected, is_roothub;
    unsigned index, hub_addr, nports;
    void *parent;
    uint8_t *int_buffer;
    struct usbh_bus *bus;
};
struct usbh_bus {
    struct { struct usbh_hub roothub; uint8_t roothub_intbuf[1]; } hcd;
    void *hub_mq, *hub_sem, *hub_thread;
    uint8_t busid;
    void (*event_handler)(uint8_t, unsigned, unsigned, unsigned, unsigned);
};
static unsigned failure, mq_live, sem_live, hc_calls, wake_calls;
static uint8_t mq_object, sem_object;

static void *usb_osal_mq_create(unsigned depth)
{
    CHECK(depth == 7U && mq_live == 0U);
    if (failure == 1U) return NULL;
    mq_live = 1U;
    return &mq_object;
}
static void *usb_osal_sem_create(unsigned count)
{
    CHECK(count == 0U && sem_live == 0U);
    if (failure == 2U) return NULL;
    sem_live = 1U;
    return &sem_object;
}
static void usb_osal_mq_delete(void *queue)
{
    CHECK(queue == &mq_object && mq_live == 1U);
    mq_live = 0U;
}
static void usb_osal_sem_delete(void *semaphore)
{
    CHECK(semaphore == &sem_object && sem_live == 1U);
    sem_live = 0U;
}
static int usb_hc_init(struct usbh_bus *bus)
{
    CHECK(bus->hub_mq != NULL && bus->hub_sem != NULL);
    ++hc_calls;
    return failure == 3U ? -1 : 0;
}
static void usbh_hub_thread_wakeup(struct usbh_hub *hub)
{
    CHECK(hub->connected && hub->is_roothub && hub->int_buffer[0] == 2U);
    ++wake_calls;
}
static void event_handler(uint8_t busid, unsigned h, unsigned p,
                          unsigned i, unsigned event)
{
    CHECK(busid == 0U && h == 0U && p == 0U && i == 0U);
    CHECK(event == USBH_EVENT_INIT);
}

#include "usb1_hub_initialize.inc"

int main(void)
{
    for (failure = 1U; failure <= 3U; ++failure) {
        struct usbh_bus bus = {0};
        bus.event_handler = event_handler;
        hc_calls = wake_calls = 0U;
        CHECK(usbh_hub_initialize(&bus) == -1);
        CHECK(mq_live == 0U && sem_live == 0U);
        CHECK(bus.hub_mq == NULL && bus.hub_sem == NULL);
        CHECK(hc_calls == (failure == 3U ? 1U : 0U));
        CHECK(wake_calls == 0U);
    }
    failure = 0U;
    {
        struct usbh_bus bus = {0};
        bus.event_handler = event_handler;
        CHECK(usbh_hub_initialize(&bus) == 0);
        CHECK(mq_live == 1U && sem_live == 1U && wake_calls == 1U);
        usb_osal_sem_delete(bus.hub_sem);
        usb_osal_mq_delete(bus.hub_mq);
    }
    puts("PASS USB1 production hub allocation failures release all resources; retry starts");
    return 0;
}
