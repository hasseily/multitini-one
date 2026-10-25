/* SPDX-License-Identifier: GPL-3.0-only */
/* Ultimate HTTP target06; original implementation of the pinned public wire
 * contract. HTTP over TCP only: HTTPS is rejected before any connection. */
#include "http_internal.h"
#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static mt_uci_result_t answer(mt_uci_reply_t *r, const char *s) {
    mt_uci_reply_clear(r);
    mt_uci_reply_status(r, s);
    return MT_UCI_REPLY;
}
static bool equal_n(const char *a, const char *b, size_t n) {
    size_t i;
    for (i = 0; i < n; i++)
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i]))
            return false;
    return true;
}
static bool text_copy(char *dst, size_t cap, const uint8_t *src, size_t size) {
    while (size && src[size - 1] == 0)
        size--;
    if (size >= cap || memchr(src, 0, size))
        return false;
    memcpy(dst, src, size);
    dst[size] = 0;
    return true;
}
static const char *field(http_header_t *h, const char *key, size_t *length) {
    const char *p = h->lines;
    size_t n = strlen(key);
    while (*p) {
        const char *end = strstr(p, "\r\n"), *colon = strchr(p, ':');
        if (!end)
            return NULL;
        if (colon && colon < end && (size_t)(colon - p) == n && equal_n(p, key, n)) {
            p = colon + 1;
            while (p < end && (*p == ' ' || *p == '\t'))
                p++;
            *length = (size_t)(end - p);
            return p;
        }
        p = end + 2;
    }
    return NULL;
}
static bool set_field(http_header_t *h, const char *key, const char *value) {
    char output[4096];
    size_t at = 0, n = strlen(key), v = strlen(value), i;
    const char *p = h->lines;
    if (!n || n > 255 || strchr(value, '\r') || strchr(value, '\n'))
        return false;
    for (i = 0; i < n; i++)
        if (!isalnum((unsigned char)key[i]) && !strchr("!#$%&'*+-.^_`|~", key[i]))
            return false;
    while (*p) {
        const char *end = strstr(p, "\r\n"), *colon = strchr(p, ':');
        size_t bytes;
        if (!end)
            return false;
        bytes = (size_t)(end + 2 - p);
        if (!(colon && colon < end && (size_t)(colon - p) == n && equal_n(p, key, n))) {
            if (at + bytes >= sizeof(output))
                return false;
            memcpy(output + at, p, bytes);
            at += bytes;
        }
        p = end + 2;
    }
    if (at + n + 2 + v + 2 >= sizeof(output))
        return false;
    memcpy(output + at, key, n);
    at += n;
    memcpy(output + at, ": ", 2);
    at += 2;
    memcpy(output + at, value, v);
    at += v;
    memcpy(output + at, "\r\n", 3);
    at += 2;
    memcpy(h->lines, output, at + 1);
    h->length = at;
    return true;
}
static http_header_t *header_new(uint8_t verb, const char *url) {
    http_header_t *h = calloc(1, sizeof(*h));
    const char *start = url, *slash, *colon;
    char hostport[300];
    size_t n;
    unsigned long port;
    char *end;
    if (!h)
        return NULL;
    if (verb < 1 || verb > 9)
        goto bad;
    h->verb = verb;
    h->port = 80;
    if (!strncmp(start, "https://", 8)) {
        h->secure = true;
        h->port = 443;
        start += 8;
    } else if (!strncmp(start, "http://", 7))
        start += 7;
    if (strchr(start, '\r') || strchr(start, '\n') || strchr(start, '@') || strchr(start, '#') ||
        strchr(start, ' '))
        goto bad;
    slash = strchr(start, '/');
    n = slash ? (size_t)(slash - start) : strlen(start);
    if (!n || n >= sizeof(hostport))
        goto bad;
    memcpy(hostport, start, n);
    hostport[n] = 0;
    colon = strchr(hostport, ':');
    if (colon) {
        port = strtoul(colon + 1, &end, 10);
        if (!colon[1] || *end || !port || port > 65535)
            goto bad;
        h->port = (uint16_t)port;
        n = (size_t)(colon - hostport);
    }
    if (!n || n >= sizeof(h->host))
        goto bad;
    memcpy(h->host, hostport, n);
    h->host[n] = 0;
    if (slash) {
        if (strlen(slash) >= sizeof(h->path))
            goto bad;
        strcpy(h->path, slash);
    } else
        strcpy(h->path, "/");
    if (!set_field(h, "Host", hostport))
        goto bad;
    return h;
bad:
    free(h);
    return NULL;
}
static void exchange_clear(mt_http_t *h) {
    if (h->socket > 0)
        (void)mt_net_close(h->net, h->socket);
    h->socket = 0;
    free(h->request);
    h->request = NULL;
    free(h->object);
    h->object = NULL;
    free(h->reply_data);
    h->reply_data = NULL;
    h->phase = 0;
    h->request_size = h->request_sent = h->input_size = h->input_at = h->output_size =
        h->object_size = h->reply_size = h->reply_offset = 0;
    h->receive_wait = h->send_wait = false;
}
static mt_uci_result_t fail(mt_http_t *h, mt_uci_reply_t *r, const char *s) {
    exchange_clear(h);
    return answer(r, s);
}
static bool make_request(mt_http_t *h, http_header_t *header, http_body_t *body) {
    static const char *verbs[] = {"GET",  "PUT",     "POST",    "PATCH", "DELETE",
                                  "HEAD", "OPTIONS", "CONNECT", "TRACE"};
    static const char *types[] = {"", "application/octet-stream", "application/json",
                                  "application/json", "application/x-www-form-urlencoded"};
    uint8_t *rendered = NULL;
    size_t body_size = 0, ignore, cap, at;
    char extra[180] = "";
    int added;
    if (body) {
        if (body->format == 1) {
            body_size = body->length;
        } else {
            rendered = http_json_render(body->root, body->format == 4, &body_size);
            if (!rendered)
                return false;
        }
        if (!field(header, "Content-Type", &ignore))
            snprintf(extra, sizeof(extra), "Content-Type: %s\r\n", types[body->format]);
        if (!field(header, "Content-Length", &ignore)) {
            size_t used = strlen(extra);
            snprintf(extra + used, sizeof(extra) - used, "Content-Length: %u\r\n",
                     (unsigned)body_size);
        }
    }
    cap = strlen(header->path) + header->length + strlen(extra) + body_size + 128;
    h->request = malloc(cap);
    if (!h->request) {
        free(rendered);
        return false;
    }
    added = snprintf((char *)h->request, cap, "%s %s HTTP/1.1\r\n%s%s%s\r\n",
                     verbs[header->verb - 1], header->path, header->lines, extra,
                     field(header, "Connection", &ignore) ? "" : "Connection: close\r\n");
    if (added < 0 || (size_t)added >= cap) {
        free(rendered);
        return false;
    }
    at = (size_t)added;
    if (body_size)
        memcpy(h->request + at, rendered ? rendered : body->binary, body_size);
    h->request_size = at + body_size;
    free(rendered);
    return true;
}
static int input(mt_http_t *h) {
    int got;
    if (h->input_at < h->input_size)
        return 1;
    if (!h->receive_wait) {
        got = mt_net_receive(h->net, h->socket, sizeof(h->input));
        if (got && got != MT_NET_AGAIN)
            return -1;
        h->receive_wait = true;
    }
    got = mt_net_receive_result(h->net, h->socket, h->input, sizeof(h->input), NULL);
    if (got == MT_NET_AGAIN)
        return 0;
    h->receive_wait = false;
    if (got < 0)
        return -1;
    if (!got)
        return 2;
    h->input_at = 0;
    h->input_size = (size_t)got;
    h->deadline = h->net->now + UINT64_C(10000000);
    return 1;
}
static bool parse_response(mt_http_t *h) {
    char *line = h->response_header, *end = strstr(line, "\r\n"), *code;
    bool have_length = false, have_te = false;
    unsigned response;
    uint64_t length = 0;
    if (!end || strncmp(line, "HTTP/1.", 7) || (line[7] != '0' && line[7] != '1') || line[8] != ' ')
        return false;
    code = line + 9;
    if (!isdigit((unsigned char)code[0]) || !isdigit((unsigned char)code[1]) ||
        !isdigit((unsigned char)code[2]) || code + 3 > end)
        return false;
    response = (unsigned)(code[0] - '0') * 100 + (unsigned)(code[1] - '0') * 10 +
               (unsigned)(code[2] - '0');
    {
        size_t size = (size_t)(end - code);
        if (size >= sizeof(h->response_status))
            size = sizeof(h->response_status) - 1;
        memcpy(h->response_status, code, size);
        h->response_status[size] = 0;
    }
    line = end + 2;
    h->chunked = false;
    while (*line) {
        char *colon;
        size_t key;
        end = strstr(line, "\r\n");
        if (!end)
            return false;
        if (end == line)
            break;
        colon = memchr(line, ':', (size_t)(end - line));
        if (!colon)
            return false;
        key = (size_t)(colon - line);
        code = colon + 1;
        while (code < end && (*code == ' ' || *code == '\t'))
            code++;
        if (key == 14 && equal_n(line, "Content-Length", 14)) {
            uint64_t parsed = 0;
            char *p = code;
            if (p == end)
                return false;
            while (p < end && isdigit((unsigned char)*p)) {
                unsigned d = (unsigned)(*p++ - '0');
                if (parsed > (UINT64_MAX - d) / 10)
                    return false;
                parsed = parsed * 10 + d;
            }
            while (p < end && (*p == ' ' || *p == '\t'))
                p++;
            if (p != end || (have_length && parsed != length))
                return false;
            length = parsed;
            have_length = true;
        }
        if (key == 17 && equal_n(line, "Transfer-Encoding", 17)) {
            size_t size = (size_t)(end - code);
            while (size && (code[size - 1] == ' ' || code[size - 1] == '\t'))
                size--;
            if (have_te || size != 7 || !equal_n(code, "chunked", 7))
                return false;
            have_te = true;
            h->chunked = true;
        }
        line = end + 2;
    }
    /* Refuse ambiguous framing rather than apply inconsistent CL/TE rules. */
    if (have_te && have_length)
        return false;
    if (response >= 100 && response < 200) {
        if (response == 101)
            return false;
        h->response_header_size = 0;
        h->response_header[0] = 0;
        return true;
    }
    h->length_known = have_length;
    h->remaining = length;
    h->body_done = (((http_header_t *)h->headers[h->request_header])->verb == 6) ||
                   response == 204 || response == 304 || (have_length && !length);
    h->phase = 4;
    h->chunk_state = 0;
    h->chunk_line_size = 0;
    h->trailers_size = 0;
    return true;
}
static bool body_byte(mt_http_t *h, uint8_t c, bool *emit_byte) {
    *emit_byte = false;
    if (!h->chunked) {
        if (h->length_known) {
            if (!h->remaining)
                return false;
            h->remaining--;
            if (!h->remaining)
                h->body_done = true;
        }
        *emit_byte = true;
        return true;
    }
    if (h->chunk_state == 0) {
        if (h->chunk_line_size + 1 >= sizeof(h->chunk_line))
            return false;
        h->chunk_line[h->chunk_line_size++] = (char)c;
        if (c == '\n') {
            size_t i = 0;
            uint64_t value = 0;
            unsigned digits = 0;
            if (h->chunk_line_size < 2 || h->chunk_line[h->chunk_line_size - 2] != '\r')
                return false;
            while (i < h->chunk_line_size - 2 && isxdigit((unsigned char)h->chunk_line[i])) {
                unsigned char x = (unsigned char)h->chunk_line[i++];
                unsigned digit =
                    isdigit(x) ? (unsigned)(x - '0') : (unsigned)(tolower((int)x) - 'a' + 10);
                if (value > (UINT64_MAX - digit) / 16)
                    return false;
                value = value * 16 + digit;
                digits++;
            }
            if (!digits || (i != h->chunk_line_size - 2 && h->chunk_line[i] != ';'))
                return false;
            h->chunk_remaining = value;
            h->chunk_line_size = 0;
            h->chunk_state = value ? 1 : 4;
        }
        return true;
    }
    if (h->chunk_state == 1) {
        *emit_byte = true;
        if (!--h->chunk_remaining)
            h->chunk_state = 2;
        return true;
    }
    if (h->chunk_state == 2) {
        if (c != '\r')
            return false;
        h->chunk_state = 3;
        return true;
    }
    if (h->chunk_state == 3) {
        if (c != '\n')
            return false;
        h->chunk_state = 0;
        return true;
    }
    if (++h->trailers_size > 4096)
        return false;
    if (h->chunk_line_size + 1 >= sizeof(h->chunk_line))
        return false;
    h->chunk_line[h->chunk_line_size++] = (char)c;
    if (c == '\n') {
        if (h->chunk_line_size < 2 || h->chunk_line[h->chunk_line_size - 2] != '\r')
            return false;
        if (h->chunk_line_size == 2)
            h->body_done = true;
        h->chunk_line_size = 0;
    }
    return true;
}
static mt_uci_result_t raw_reply(mt_http_t *h, mt_uci_reply_t *r) {
    mt_uci_reply_clear(r);
    memcpy(r->data, h->output, h->output_size);
    r->data_length = h->output_size;
    r->more = !h->body_done;
    if (h->first_block) {
        size_t size = h->response_header_size;
        if (size > 255)
            size = 255;
        memcpy(r->status, h->response_header, size);
        r->status_length = size;
    } else
        mt_uci_reply_status(r, "000 OK");
    h->first_block = false;
    h->output_size = 0;
    if (h->body_done) {
        if (h->socket > 0)
            (void)mt_net_close(h->net, h->socket);
        h->socket = 0;
        free(h->request);
        h->request = NULL;
        h->phase = 5;
    }
    return MT_UCI_REPLY;
}
static mt_uci_result_t object_reply(mt_http_t *h, mt_uci_reply_t *r) {
    http_json_t *json;
    http_body_t *body;
    http_header_t *header;
    unsigned bs, hs;
    char *p, *end;
    for (bs = 0; bs < 16 && h->bodies[bs]; bs++)
        ;
    if (bs == 16)
        return fail(h, r, "507 NO DATA SLOT");
    for (hs = 0; hs < 16 && h->headers[hs]; hs++)
        ;
    if (hs == 16)
        return fail(h, r, "507 NO HEADER SLOT");
    json = http_json_parse((const char *)h->object, h->object_size);
    if (!json)
        return fail(h, r, "400 NO VALID JSON");
    body = calloc(1, sizeof(*body));
    header = calloc(1, sizeof(*header));
    if (!body || !header) {
        free(body);
        free(header);
        http_json_free(json);
        return fail(h, r, "507 INSUFFICIENT STORAGE");
    }
    body->root = body->current = json;
    body->format = json->type == 5 ? 3 : 2;
    body->nodes = http_json_count(json);
    header->verb = 1;
    header->port = 80;
    strcpy(header->path, "/");
    p = strstr(h->response_header, "\r\n") + 2;
    while ((end = strstr(p, "\r\n")) && end != p) {
        char key[256], value[4096], *colon = memchr(p, ':', (size_t)(end - p));
        size_t n;
        if (!colon)
            break;
        n = (size_t)(colon - p);
        if (n >= sizeof(key))
            break;
        memcpy(key, p, n);
        key[n] = 0;
        colon++;
        while (colon < end && *colon == ' ')
            colon++;
        n = (size_t)(end - colon);
        memcpy(value, colon, n);
        value[n] = 0;
        if (!set_field(header, key, value)) {
            free(header);
            http_body_free(body);
            return fail(h, r, "507 INSUFFICIENT STORAGE");
        }
        p = end + 2;
    }
    h->bodies[bs] = body;
    h->headers[hs] = header;
    answer(r, h->response_status);
    r->data[0] = (uint8_t)hs;
    r->data[1] = (uint8_t)bs;
    r->data_length = 2;
    exchange_clear(h);
    return MT_UCI_REPLY;
}
static mt_uci_result_t poll(void *context, mt_uci_reply_t *r) {
    mt_http_t *h = context;
    http_header_t *header = h->headers[h->request_header];
    uint8_t ip[4];
    int result, error;
    unsigned work = 0;
    if (!h->phase)
        return answer(r, "500 NO MORE DATA");
    if (h->net->now >= h->deadline)
        return fail(h, r, "504 GATEWAY TIMEOUT");
    if (h->phase == 1) {
        result = mt_net_resolve_owned(h->net, MT_NET_OWNER_HTTP, header->host, ip);
        if (result == MT_NET_AGAIN)
            return MT_UCI_WAIT;
        if (result)
            return fail(h, r, "503 SERVICE UNAVAILABLE");
        h->socket = mt_net_open(h->net, MT_NET_OWNER_HTTP, false, ip, header->port, false);
        if (h->socket == MT_NET_AGAIN)
            return MT_UCI_WAIT;
        if (h->socket < 0)
            return fail(h, r, "503 SERVICE UNAVAILABLE");
        h->phase = 2;
        h->deadline = h->net->now + UINT64_C(10000000);
    }
    if (h->phase == 2) {
        mt_net_socket_state_t state = mt_net_state(h->net, h->socket, &error);
        if (state == MT_NET_S_CLOSED || state == MT_NET_S_FREE)
            return fail(h, r, "503 SERVICE UNAVAILABLE");
        if (state != MT_NET_S_CONNECTED)
            return MT_UCI_WAIT;
        if (h->send_wait) {
            result = mt_net_send_result(h->net, h->socket);
            if (result == MT_NET_AGAIN)
                return MT_UCI_WAIT;
            if (result < 0)
                return fail(h, r, "503 SERVICE UNAVAILABLE");
            h->request_sent += (size_t)result;
            h->send_wait = false;
            h->deadline = h->net->now + UINT64_C(10000000);
        }
        if (h->request_sent < h->request_size) {
            size_t amount = h->request_size - h->request_sent;
            if (amount > 895)
                amount = 895;
            if (mt_net_send(h->net, h->socket, h->request + h->request_sent, amount))
                return fail(h, r, "503 SERVICE UNAVAILABLE");
            h->send_wait = true;
            return MT_UCI_WAIT;
        }
        free(h->request);
        h->request = NULL;
        h->phase = 3;
        h->deadline = h->net->now + UINT64_C(10000000);
    }
    while (work++ < 512 && (h->phase == 3 || h->phase == 4)) {
        if (h->phase == 4 && h->body_done)
            return h->raw ? raw_reply(h, r) : object_reply(h, r);
        if (h->raw && h->output_size == sizeof(h->output))
            return raw_reply(h, r);
        result = input(h);
        if (!result)
            return MT_UCI_WAIT;
        if (result < 0)
            return fail(h, r, "503 SERVICE UNAVAILABLE");
        if (result == 2) {
            if (h->phase == 3 || h->chunked || (h->length_known && h->remaining))
                return fail(h, r, "502 TRUNCATED RESPONSE");
            h->body_done = true;
            continue;
        }
        {
            uint8_t c = h->input[h->input_at++];
            if (h->phase == 3) {
                size_t at = h->response_header_size;
                if (!c || at + 1 >= sizeof(h->response_header))
                    return fail(h, r, "502 BAD RESPONSE HEADER");
                h->response_header[at++] = (char)c;
                h->response_header[at] = 0;
                h->response_header_size = at;
                if (at >= 4 && !memcmp(h->response_header + at - 4, "\r\n\r\n", 4) &&
                    !parse_response(h))
                    return fail(h, r, "502 BAD RESPONSE HEADER");
            } else {
                bool emit;
                if (!body_byte(h, c, &emit))
                    return fail(h, r, "502 BAD RESPONSE BODY");
                if (emit) {
                    if (h->raw)
                        h->output[h->output_size++] = c;
                    else {
                        if (h->object_size == HTTP_BODY_LIMIT)
                            return fail(h, r, "507 RESPONSE TOO LARGE");
                        h->object[h->object_size++] = c;
                    }
                }
            }
        }
    }
    return MT_UCI_WAIT;
}
static mt_uci_result_t reply_chunk(mt_http_t *h, mt_uci_reply_t *r) {
    size_t amount = h->reply_size - h->reply_offset;
    if (amount > 895)
        amount = 895;
    answer(r, "000 OK");
    memcpy(r->data, h->reply_data + h->reply_offset, amount);
    r->data_length = amount;
    h->reply_offset += amount;
    r->more = h->reply_offset < h->reply_size;
    if (!r->more) {
        free(h->reply_data);
        h->reply_data = NULL;
    }
    return MT_UCI_REPLY;
}
static mt_uci_result_t begin(void *context, const uint8_t c[896], size_t length,
                             mt_uci_reply_t *r) {
    mt_http_t *h = context;
    unsigned cmd = length >= 2 ? c[1] : 0, slot = length >= 3 ? c[2] : 255, i;
    char text[896];
    http_header_t *header;
    http_body_t *body;
    size_t size;
    const char *value;
    exchange_clear(h);
    h->command = (uint8_t)cmd;
    if (cmd == 1) {
        mt_uci_reply_text(r, "ULTIMATE HTTP TARGET V1.0", "000 OK");
        return MT_UCI_REPLY;
    }
    if (cmd == 0x10) {
        for (i = 0; i < 16; i++) {
            free(h->headers[i]);
            h->headers[i] = NULL;
            http_body_free(h->bodies[i]);
            h->bodies[i] = NULL;
        }
        return answer(r, "000 OK");
    }
    if (cmd == 0x11) {
        if (length < 7 || slot < 1 || slot > 9 || !text_copy(text, sizeof(text), c + 3, length - 3))
            return answer(r, "400 BAD COMMAND");
        for (i = 0; i < 16 && h->headers[i]; i++)
            ;
        if (i == 16)
            return answer(r, "507 NO HEADER SLOT");
        header = header_new((uint8_t)slot, text);
        if (!header)
            return answer(r, "400 BAD COMMAND");
        if (header->secure) {
            free(header);
            return answer(r, "501 HTTPS NOT SUPPORTED");
        }
        h->headers[i] = header;
        answer(r, "000 OK");
        r->data[0] = (uint8_t)i;
        r->data_length = 1;
        return MT_UCI_REPLY;
    }
    if (cmd == 0x21) {
        if (length != 3)
            return answer(r, "400 BAD COMMAND");
        if (slot < 1 || slot > 4)
            return answer(r, "500 BAD FORMAT");
        for (i = 0; i < 16 && h->bodies[i]; i++)
            ;
        if (i == 16)
            return answer(r, "507 NO DATA SLOT");
        body = http_body_new((uint8_t)slot);
        if (!body)
            return answer(r, "507 INSUFFICIENT STORAGE");
        h->bodies[i] = body;
        answer(r, "000 OK");
        r->data[0] = (uint8_t)i;
        r->data_length = 1;
        return MT_UCI_REPLY;
    }
    if (cmd == 0x12 || cmd == 0x22) {
        if (length != 3 || slot >= 16)
            return answer(r, "400 BAD COMMAND");
        if (cmd == 0x12) {
            free(h->headers[slot]);
            h->headers[slot] = NULL;
        } else {
            http_body_free(h->bodies[slot]);
            h->bodies[slot] = NULL;
        }
        return answer(r, "000 OK");
    }
    if (cmd >= 0x13 && cmd <= 0x15) {
        if (slot >= 16 || !(header = h->headers[slot]))
            return answer(r, "400 BAD COMMAND");
        if (cmd == 0x13) {
            char *colon;
            if (length < 7 || !text_copy(text, sizeof(text), c + 3, length - 3))
                return answer(r, "400 BAD COMMAND");
            colon = strstr(text, ": ");
            if (!colon)
                return answer(r, "500 BAD FORMAT");
            *colon = 0;
            if (!set_field(header, text, colon + 2))
                return answer(r, "500 BAD FORMAT");
            return answer(r, "000 OK");
        }
        if (cmd == 0x14) {
            if (length < 4 || !text_copy(text, sizeof(text), c + 3, length - 3))
                return answer(r, "400 BAD COMMAND");
            value = field(header, text, &size);
            if (!value)
                return answer(r, "404 KEY NOT PRESENT");
            if (size > 895)
                size = 895;
            answer(r, "000 OK");
            memcpy(r->data, value, size);
            r->data_length = size;
            return MT_UCI_REPLY;
        }
        if (length != 4)
            return answer(r, "400 BAD COMMAND");
        {
            const char *p = header->lines, *end;
            unsigned index = 1;
            size_t at = 0;
            while ((end = strstr(p, "\r\n"))) {
                if (!c[3] || c[3] == index) {
                    size_t count = (size_t)(end - p);
                    if (at + count + 1 > 895)
                        break;
                    memcpy(r->data + at, p, count);
                    at += count;
                    r->data[at++] = '\r';
                }
                p = end + 2;
                index++;
            }
            if (c[3] >= index)
                return answer(r, "400 INDEX OUT OF BOUNDS");
            r->data_length = at;
            r->status_length = 0;
            r->more = false;
            mt_uci_reply_status(r, "000 OK");
            return MT_UCI_REPLY;
        }
    }
    if (cmd >= 0x23 && cmd <= 0x2e) {
        if (length < 3 || slot >= 16 || !(body = h->bodies[slot]))
            return answer(r, "400 BAD REQUEST");
        if (!http_body_command(body, (uint8_t)cmd, c + 3, length - 3, &h->reply_data,
                               &h->reply_size))
            return answer(r, "400 BAD FORMAT");
        h->reply_offset = 0;
        if (h->reply_data)
            return reply_chunk(h, r);
        return answer(r, "000 OK");
    }
    if (cmd == 0x31 || cmd == 0x32) {
        if (length != 4 || slot >= 16 || !(header = h->headers[slot]))
            return answer(r, "400 BAD COMMAND");
        if (header->secure)
            return answer(r, "501 HTTPS NOT SUPPORTED");
        body = c[3] < 16 ? h->bodies[c[3]] : NULL;
        if (!make_request(h, header, body))
            return fail(h, r, "507 INSUFFICIENT STORAGE");
        h->raw = cmd == 0x32;
        h->request_header = (uint8_t)slot;
        h->request_body = c[3];
        h->phase = 1;
        h->deadline = h->net->now + UINT64_C(10000000);
        h->response_header_size = 0;
        h->response_header[0] = 0;
        h->body_done = false;
        h->first_block = true;
        if (!h->raw) {
            h->object = malloc(HTTP_BODY_LIMIT);
            if (!h->object)
                return fail(h, r, "507 INSUFFICIENT STORAGE");
        }
        return poll(h, r);
    }
    return answer(r, "400 BAD COMMAND");
}
static mt_uci_result_t next(void *context, size_t consumed, mt_uci_reply_t *r) {
    mt_http_t *h = context;
    (void)consumed;
    if (h->reply_data)
        return reply_chunk(h, r);
    if (h->phase == 4)
        return poll(h, r);
    return answer(r, "500 NO MORE DATA");
}
static mt_uci_result_t abort_cmd(void *context, size_t consumed) {
    mt_http_t *h = context;
    (void)consumed;
    exchange_clear(h);
    mt_net_resolve_cancel_owner(h->net, MT_NET_OWNER_HTTP);
    return MT_UCI_REPLY;
}
static void reset(void *context) {
    mt_http_t *h = context;
    unsigned i;
    abort_cmd(h, 0);
    mt_net_close_owner(h->net, MT_NET_OWNER_HTTP);
    for (i = 0; i < 16; i++) {
        free(h->headers[i]);
        h->headers[i] = NULL;
        http_body_free(h->bodies[i]);
        h->bodies[i] = NULL;
    }
}
void mt_http_init(mt_http_t *h, mt_net_t *n) {
    memset(h, 0, sizeof(*h));
    h->net = n;
}
mt_uci_target_t mt_http_uci_target(mt_http_t *h) {
    mt_uci_target_t target = {h, begin, poll, next, abort_cmd, reset};
    return target;
}
