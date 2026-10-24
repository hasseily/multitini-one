/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_net.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
bool mt_net_ipv4(const char *s, uint8_t out[4]) {
    uint8_t value[4];
    unsigned i;
    if (!s)
        return false;
    for (i = 0; i < 4; i++) {
        unsigned n = 0, count = 0;
        while (*s >= '0' && *s <= '9') {
            n = n * 10 + (unsigned)(*s++ - '0');
            if (++count > 3 || n > 255)
                return false;
        }
        if (!count || (i < 3 ? *s++ != '.' : *s != 0))
            return false;
        value[i] = (uint8_t)n;
    }
    memcpy(out, value, 4);
    return true;
}
bool mt_net_mac_valid(const uint8_t m[6]) {
    unsigned i, n = 0;
    if (m[0] & 1)
        return false;
    for (i = 0; i < 6; i++)
        n |= m[i];
    return n != 0;
}
void mt_net_config_default(mt_net_config_t *c) {
    memset(c, 0, sizeof(*c));
    c->mode = MT_NET_OFF;
}
void mt_net_generate_mac(mt_net_config_t *c, const void *seed, size_t n, uint64_t time) {
    const uint8_t *p = seed;
    uint64_t h = UINT64_C(1469598103934665603);
    size_t i;
    if (c->mac_explicit && c->mac_valid)
        return;
    for (i = 0; i < n; i++)
        h = (h ^ p[i]) * UINT64_C(1099511628211);
    c->mac_card = (uint32_t)h ^ (uint32_t)(h >> 32);
    h ^= time;
    for (i = 0; i < 6; i++) {
        h ^= h >> 12;
        h ^= h << 25;
        h ^= h >> 27;
        c->mac[i] = (uint8_t)(h * UINT64_C(2685821657736338717));
    }
    c->mac[0] = (c->mac[0] & 0xfc) | 2;
    c->mac_valid = true;
    if (!c->hostname[0])
        snprintf(c->hostname, sizeof(c->hostname), "multitini-%02x%02x%02x", c->mac[3], c->mac[4],
                 c->mac[5]);
}
static bool mac_parse(const char *s, uint8_t m[6]) {
    unsigned i;
    for (i = 0; i < 6; i++) {
        char a = s[0], b = a ? s[1] : 0;
        unsigned x, y;
        if (!isxdigit((unsigned char)a) || !isxdigit((unsigned char)b))
            return false;
        x = isdigit((unsigned char)a) ? (unsigned)(a - '0')
                                      : (unsigned)(tolower((unsigned char)a) - 'a' + 10);
        y = isdigit((unsigned char)b) ? (unsigned)(b - '0')
                                      : (unsigned)(tolower((unsigned char)b) - 'a' + 10);
        m[i] = (uint8_t)(x * 16 + y);
        s += 2;
        if (i < 5) {
            if (*s++ != ':')
                return false;
        } else if (*s)
            return false;
    }
    return mt_net_mac_valid(m);
}
bool mt_net_config_set(mt_net_config_t *c, const char *key, const char *v) {
    mt_net_config_t t = *c;
    size_t i, n = strlen(v);
    if (!strcmp(key, "mode")) {
        if (!strcmp(v, "off"))
            t.mode = MT_NET_OFF;
        else if (!strcmp(v, "dhcp"))
            t.mode = MT_NET_DHCP;
        else if (!strcmp(v, "static"))
            t.mode = MT_NET_STATIC;
        else
            return false;
    } else if (!strcmp(key, "ip")) {
        if (!mt_net_ipv4(v, t.ip))
            return false;
    } else if (!strcmp(key, "netmask")) {
        if (!mt_net_ipv4(v, t.mask))
            return false;
    } else if (!strcmp(key, "gateway")) {
        if (!mt_net_ipv4(v, t.gateway))
            return false;
    } else if (!strcmp(key, "dns1")) {
        if (!mt_net_ipv4(v, t.dns[0]))
            return false;
    } else if (!strcmp(key, "dns2")) {
        if (!mt_net_ipv4(v, t.dns[1]))
            return false;
    } else if (!strcmp(key, "hostname")) {
        if (n > 63)
            return false;
        for (i = 0; i < n; i++)
            if (!isalnum((unsigned char)v[i]) && v[i] != '-')
                return false;
        memcpy(t.hostname, v, n + 1);
    } else if (!strcmp(key, "mac") || !strcmp(key, "mac_auto")) {
        uint8_t m[6];
        if (!mac_parse(v, m))
            return false;
        if (!t.mac_explicit || !strcmp(key, "mac")) {
            memcpy(t.mac, m, 6);
            t.mac_valid = true;
            t.mac_explicit = !strcmp(key, "mac");
        }
    } else if (!strcmp(key, "mac_card")) {
        char *end;
        unsigned long x;
        if (n != 8)
            return false;
        x = strtoul(v, &end, 16);
        if (*end)
            return false;
        t.mac_card = (uint32_t)x;
    } else
        return false;
    *c = t;
    return true;
}
bool mt_net_config_parse(mt_net_config_t *c, const char *text, size_t length, bool *unknown) {
    mt_net_config_t t;
    size_t p = 0;
    mt_net_config_default(&t);
    if (unknown)
        *unknown = false;
    while (p < length) {
        char line[384], *eq, *key, *end;
        size_t n = 0;
        while (p < length && text[p] != '\n') {
            if (!text[p] || n + 1 >= sizeof(line))
                return false;
            line[n++] = text[p++];
        }
        if (p < length)
            p++;
        line[n] = 0;
        key = line;
        while (*key == ' ' || *key == '\t')
            key++;
        end = key + strlen(key);
        while (end > key && isspace((unsigned char)end[-1]))
            *--end = 0;
        if (!*key || *key == '#' || *key == ';')
            continue;
        eq = strchr(key, '=');
        if (!eq)
            return false;
        *eq++ = 0;
        end = key + strlen(key);
        while (end > key && isspace((unsigned char)end[-1]))
            *--end = 0;
        while (*eq == ' ' || *eq == '\t')
            eq++;
        if (strcmp(key, "mode") && strcmp(key, "ip") && strcmp(key, "netmask") &&
            strcmp(key, "gateway") && strcmp(key, "dns1") && strcmp(key, "dns2") &&
            strcmp(key, "hostname") && strcmp(key, "mac") && strcmp(key, "mac_auto") &&
            strcmp(key, "mac_card")) {
            if (unknown)
                *unknown = true;
            continue;
        }
        if (!mt_net_config_set(&t, key, eq))
            return false;
    }
    *c = t;
    return true;
}
size_t mt_net_config_format(const mt_net_config_t *c, char *out, size_t cap) {
    char mac[100] = "";
    int n;
    if (c->mac_valid)
        snprintf(mac, sizeof(mac), "%s=%02x:%02x:%02x:%02x:%02x:%02x\nmac_card=%08lx\n",
                 c->mac_explicit ? "mac" : "mac_auto", c->mac[0], c->mac[1], c->mac[2], c->mac[3],
                 c->mac[4], c->mac[5], (unsigned long)c->mac_card);
#define IP(x) (x)[0], (x)[1], (x)[2], (x)[3]
    n = snprintf(out, cap,
                 "mode=%s\nip=%u.%u.%u.%u\nnetmask=%u.%u.%u.%u\ngateway=%u.%u.%u.%u\ndns1=%u.%u.%u."
                 "%u\ndns2=%u.%u.%u.%u\nhostname=%s\n%s",
                 c->mode == MT_NET_DHCP     ? "dhcp"
                 : c->mode == MT_NET_STATIC ? "static"
                                            : "off",
                 IP(c->ip), IP(c->mask), IP(c->gateway), IP(c->dns[0]), IP(c->dns[1]), c->hostname,
                 mac);
#undef IP
    return n < 0 || (size_t)n >= cap ? 0 : (size_t)n;
}
