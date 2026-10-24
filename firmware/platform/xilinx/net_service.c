/* SPDX-License-Identifier: GPL-3.0-only */
#include "net_service.h"
#include "multitini_c64_interface.h"
#include "multitini_http.h"
#include "multitini_net_uci.h"
#include "sd_storage.h"
#include "xiltimer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static mt_core_link_t *link;
static mt_net_t network;
static mt_net_uci_t network_target;
static mt_http_t http_target;
static mt_net_config_t configuration;
static bool initialized, present, settings_warning;
static uint8_t identity[64];
static size_t identity_size;
static char lookup[MT_NET_NAME];
static mt_net_output_fn lookup_output;
static void *lookup_context;
static const char config_path[] = "0:/multitini/network.ini",
                  journal_path[] = "0:/multitini/network.ini.mtj";
static uint64_t now_us(void *context) {
    XTime value;
    uint64_t hz = COUNTS_PER_SECOND;
    (void)context;
    XTime_GetTime(&value);
    return (value / hz) * UINT64_C(1000000) + ((value % hz) * UINT64_C(1000000)) / hz;
}
static void barrier(void) {
#if defined(__arm__) || defined(__aarch64__)
    __asm__ volatile("dmb sy" ::: "memory");
#endif
}
static uint32_t read_status(void) { return link->read32(link->context, MT_REG_NET_STATUS); }
static int access_byte(void *context, uint16_t address, bool write, uint8_t data, uint8_t *out) {
    uint64_t deadline;
    uint32_t status, before;
    unsigned spins;
    (void)context;
    if (!present || address & 0x8000)
        return -1;
    deadline = now_us(NULL) + 2;
    for (spins = 0; spins < 256; spins++) {
        status = read_status();
        if (status & MT_NET_STATUS_READY)
            break;
        if (now_us(NULL) >= deadline)
            return -1;
    }
    if (spins == 256)
        return -1;
    before = status & UINT32_C(0x00ff0000);
    barrier();
    link->write32(link->context, MT_REG_NET_ACCESS,
                  address | ((uint32_t)data << 16) | (write ? MT_NET_ACCESS_WRITE : 0));
    barrier();
    deadline = now_us(NULL) + 2;
    for (spins = 0; spins < 256; spins++) {
        status = read_status();
        if ((status & MT_NET_STATUS_DONE) && (status & UINT32_C(0x00ff0000)) != before) {
            if (status & MT_NET_STATUS_ERROR)
                return -1;
            *out = (uint8_t)(status >> 8);
            return 0;
        }
        if (now_us(NULL) >= deadline)
            return -1;
    }
    return -1;
}
static void set_run(void *context, bool enabled) {
    (void)context;
    if (present) {
        link->write32(link->context, MT_REG_NET_CONTROL, enabled ? MT_NET_CONTROL_RUN : 0);
        barrier();
    }
}
static bool ready(void *context) {
    (void)context;
    return present && (read_status() & (MT_NET_STATUS_READY | MT_NET_STATUS_RESET_DONE)) ==
                          (MT_NET_STATUS_READY | MT_NET_STATUS_RESET_DONE);
}
static bool save_config(const mt_net_config_t *c) {
    char data[1024];
    size_t size;
    if (mt_sd_storage_external_active())
        return false;
    size = mt_net_config_format(c, data, sizeof(data));
    if (!size || mt_sd_storage_ensure_directory("0:/multitini").status != MT_SD_OK)
        return false;
    return mt_sd_storage_journal_replace(config_path, journal_path, (const uint8_t *)data, size)
               .status == MT_SD_OK;
}
static void load_config(void) {
    uint8_t data[1024];
    bool found = false, unknown = false;
    mt_sd_result_t result;
    mt_net_config_default(&configuration);
    settings_warning = false;
    result = mt_sd_storage_journal_recover(config_path, journal_path, data, sizeof(data));
    if (result.status != MT_SD_OK) {
        settings_warning = true;
        return;
    }
    result = mt_sd_storage_read_file_optional_max(config_path, data, sizeof(data), &found);
    if (result.status != MT_SD_OK) {
        settings_warning = true;
        return;
    }
    if (found && !mt_net_config_parse(&configuration, (const char *)data, result.size, &unknown)) {
        mt_net_config_default(&configuration);
        settings_warning = true;
    }
    settings_warning |= unknown;
}
void mt_net_service_seed(const void *data, size_t size) {
    if (size > sizeof(identity))
        size = sizeof(identity);
    if (data && size) {
        memcpy(identity, data, size);
        identity_size = size;
    }
}
static bool ensure_mac(mt_net_config_t *c) {
    mt_net_config_t generated = *c;
    if (c->mac_valid && c->mac_explicit)
        return true;
    if (!identity_size)
        return false;
    mt_net_generate_mac(&generated, identity, identity_size, now_us(NULL));
    if (c->mac_valid && c->mac_card == generated.mac_card)
        return true;
    *c = generated;
    return true;
}
static void apply_config(void) {
    mt_net_config_t effective = configuration;
    mt_uci_target_t nt = mt_net_uci_target(&network_target), ht = mt_http_uci_target(&http_target);
    nt.reset(nt.context);
    ht.reset(ht.context);
    lookup[0] = 0;
    lookup_output = NULL;
    if (!present)
        effective.mode = MT_NET_OFF;
    mt_net_configure(&network, &effective);
}
void mt_net_service_init(mt_core_link_t *new_link) {
    mt_net_ops_t ops = {NULL, access_byte, now_us, set_run, ready};
    mt_net_config_t effective;
    uint32_t abi;
    if (initialized)
        mt_net_service_shutdown();
    link = new_link;
    present = false;
    load_config();
    if (link && link->read32 && link->write32) {
        abi = link->read32(link->context, MT_REG_ABI_VERSION);
        if ((abi >> 16) == MT_ABI_MAJOR && (abi & 0xffff) >= MT_NET_MIN_ABI_MINOR &&
            link->read32(link->context, MT_REG_CORE_ID) == MT_C64_CORE_ID)
            present = link->read32(link->context, MT_REG_NET_CAPS) == MT_NET_CAPS_W5100S;
    }
    if (configuration.mode != MT_NET_OFF) {
        mt_net_config_t before = configuration;
        if (!ensure_mac(&configuration) ||
            (memcmp(&before, &configuration, sizeof(before)) && !save_config(&configuration))) {
            configuration.mode = MT_NET_OFF;
            settings_warning = true;
        }
    }
    effective = configuration;
    if (!present)
        effective.mode = MT_NET_OFF;
    mt_net_init(&network, &ops, &effective);
    mt_net_uci_init(&network_target, &network);
    mt_http_init(&http_target, &network);
    initialized = true;
}
void mt_net_service_poll(void) {
    if (!initialized)
        return;
    mt_net_poll(&network);
    if (lookup[0] && lookup_output) {
        uint8_t ip[4];
        int result = mt_net_resolve(&network, lookup, ip);
        if (result != MT_NET_AGAIN) {
            char line[320];
            if (result)
                snprintf(line, sizeof(line), "%s: unresolved host", lookup);
            else
                snprintf(line, sizeof(line), "%s: %u.%u.%u.%u", lookup, ip[0], ip[1], ip[2], ip[3]);
            lookup_output(lookup_context, line);
            lookup[0] = 0;
            lookup_output = NULL;
        }
    }
}
void mt_net_service_guest_reset(void) {
    mt_uci_target_t n, h;
    if (!initialized)
        return;
    n = mt_net_uci_target(&network_target);
    h = mt_http_uci_target(&http_target);
    n.reset(n.context);
    h.reset(h.context);
}
void mt_net_service_shutdown(void) {
    if (!initialized)
        return;
    mt_net_service_guest_reset();
    mt_net_shutdown(&network);
    lookup[0] = 0;
    lookup_output = NULL;
    present = false;
    initialized = false;
}
bool mt_net_service_set_mode(mt_net_mode_t mode) {
    mt_net_config_t next = configuration;
    if (mode > MT_NET_STATIC || !initialized)
        return false;
    next.mode = mode;
    if (mode != MT_NET_OFF && !ensure_mac(&next))
        return false;
    if (!save_config(&next))
        return false;
    configuration = next;
    settings_warning = false;
    apply_config();
    return true;
}
bool mt_net_service_cycle_mode(void) {
    return mt_net_service_set_mode((mt_net_mode_t)((configuration.mode + 1) % 3));
}
void mt_net_service_renew(void) {
    if (initialized)
        mt_net_renew(&network);
}
mt_net_mode_t mt_net_service_mode(void) { return configuration.mode; }
void mt_net_service_status(char *out, size_t cap) {
    if (!out || !cap)
        return;
    if (settings_warning)
        snprintf(out, cap, "Network settings warning");
    else if (configuration.mode == MT_NET_OFF)
        snprintf(out, cap, "Off");
    else if (!present)
        snprintf(out, cap, "Unavailable in this core");
    else if (network.failures >= 3)
        snprintf(out, cap, "W5100S error; change mode to retry");
    else if (!network.ready)
        snprintf(out, cap, "Starting Ethernet");
    else if (!network.ip[0] && !network.ip[1] && !network.ip[2] && !network.ip[3])
        snprintf(out, cap,
                 configuration.mode == MT_NET_STATIC ? "Static: address not set"
                                                     : "DHCP: waiting for lease");
    else
        snprintf(out, cap, "%s %u.%u.%u.%u, %s",
                 network.runtime_address             ? "C64"
                 : configuration.mode == MT_NET_DHCP ? "DHCP"
                                                     : "Static",
                 network.ip[0], network.ip[1], network.ip[2], network.ip[3],
                 network.link ? "link up" : "link down");
}
static void show_status(mt_net_output_fn output, void *context) {
    char line[320];
    unsigned i;
    mt_net_service_status(line, sizeof(line));
    output(context, line);
    snprintf(line, sizeof(line), "MAC %02x:%02x:%02x:%02x:%02x:%02x (%s), hostname %s",
             configuration.mac[0], configuration.mac[1], configuration.mac[2], configuration.mac[3],
             configuration.mac[4], configuration.mac[5],
             configuration.mac_explicit ? "configured" : "generated", configuration.hostname);
    output(context, line);
#define IP(x) (x)[0], (x)[1], (x)[2], (x)[3]
    snprintf(line, sizeof(line), "IP %u.%u.%u.%u mask %u.%u.%u.%u gateway %u.%u.%u.%u",
             IP(network.ip), IP(network.mask), IP(network.gateway));
    output(context, line);
    snprintf(
        line, sizeof(line), "DNS %u.%u.%u.%u / %u.%u.%u.%u, lease %llu s, T1 %llu s, T2 %llu s",
        IP(network.dns[0]), IP(network.dns[1]),
        (unsigned long long)(network.lease_expiry > network.now
                                 ? (network.lease_expiry - network.now) / 1000000
                                 : 0),
        (unsigned long long)(network.t1 > network.now ? (network.t1 - network.now) / 1000000 : 0),
        (unsigned long long)(network.t2 > network.now ? (network.t2 - network.now) / 1000000 : 0));
    output(context, line);
#undef IP
    for (i = 0; i < 16; i++) {
        mt_net_socket_t *s = &network.sockets[i];
        if (s->state != MT_NET_S_FREE) {
            snprintf(line, sizeof(line),
                     "socket %u hw %d owner %u %s state %u local %u peer %u.%u.%u.%u:%u", i,
                     s->hardware, (unsigned)s->owner, s->udp ? "UDP" : "TCP", (unsigned)s->state,
                     s->local_port, s->peer[0], s->peer[1], s->peer[2], s->peer[3], s->port);
            output(context, line);
        }
    }
}
bool mt_net_service_command(const char *text, mt_net_output_fn output, void *context) {
    char key[32], value[256];
    size_t size;
    const char *space;
    if (!text || !output)
        return false;
    if (*text == ':')
        text++;
    if (strncmp(text, "net", 3) || (text[3] && text[3] != ' '))
        return false;
    text += 3;
    while (*text == ' ')
        text++;
    if (!*text) {
        show_status(output, context);
        return true;
    }
    if (!strcmp(text, "renew")) {
        mt_net_service_renew();
        output(context, "Network renew requested");
        return true;
    }
    if (!strncmp(text, "resolve ", 8)) {
        text += 8;
        size = strlen(text);
        if (!size || size >= sizeof(lookup) || lookup[0] || network.query_pending) {
            output(context, "Resolve busy or invalid name");
            return true;
        }
        memcpy(lookup, text, size + 1);
        lookup_output = output;
        lookup_context = context;
        output(context, "Resolving");
        return true;
    }
    if (!strncmp(text, "set ", 4)) {
        mt_net_config_t next = configuration;
        text += 4;
        space = strchr(text, ' ');
        if (!space || (size = (size_t)(space - text)) >= sizeof(key) || !size) {
            output(context, "Usage: :net set KEY VALUE");
            return true;
        }
        memcpy(key, text, size);
        key[size] = 0;
        while (*space == ' ')
            space++;
        if (strlen(space) >= sizeof(value)) {
            output(context, "Invalid value");
            return true;
        }
        strcpy(value, space);
        if (!strcmp(key, "mac_auto") || !strcmp(key, "mac_card") ||
            !mt_net_config_set(&next, key, value)) {
            output(context, "Invalid network key or value");
            return true;
        }
        if (next.mode != MT_NET_OFF && !ensure_mac(&next)) {
            output(context, "Could not save generated MAC");
            return true;
        }
        if (!save_config(&next)) {
            output(context, "Could not save network settings");
            return true;
        }
        configuration = next;
        settings_warning = false;
        apply_config();
        output(context, "Network settings saved");
        return true;
    }
    output(context, "Usage: :net [set KEY VALUE | renew | resolve NAME]");
    return true;
}
mt_uci_target_t mt_net_service_network_target(void) { return mt_net_uci_target(&network_target); }
mt_uci_target_t mt_net_service_http_target(void) { return mt_http_uci_target(&http_target); }
