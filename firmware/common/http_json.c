/* SPDX-License-Identifier: GPL-3.0-only */
/* Bounded JSON tree and Ultimate HTTP typed-value codec. No imported code. */
#include "http_internal.h"
#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static char *copy_text(const void *p, size_t size) {
    char *s;
    if (size > HTTP_BODY_LIMIT)
        return NULL;
    s = malloc(size + 1);
    if (s) {
        memcpy(s, p, size);
        s[size] = 0;
    }
    return s;
}
http_json_t *http_json_new(uint8_t type) {
    http_json_t *j = calloc(1, sizeof(*j));
    if (j)
        j->type = type;
    return j;
}
void http_json_free(http_json_t *j) {
    if (!j)
        return;
    while (j->child) {
        http_json_t *c = j->child;
        j->child = c->next;
        http_json_free(c);
    }
    free(j->key);
    free(j->text);
    free(j);
}
unsigned http_json_count(http_json_t *j) {
    unsigned count = 1;
    http_json_t *c;
    for (c = j->child; c; c = c->next)
        count += http_json_count(c);
    return count;
}
static http_json_t *child_key(http_json_t *j, const char *key) {
    http_json_t *c;
    for (c = j->child; c; c = c->next)
        if (c->key && !strcmp(c->key, key))
            return c;
    return NULL;
}
static void detach(http_json_t *j) {
    http_json_t **p;
    if (!j || !j->parent)
        return;
    p = &j->parent->child;
    while (*p && *p != j)
        p = &(*p)->next;
    if (*p)
        *p = j->next;
    j->parent = NULL;
    j->next = NULL;
}
static unsigned subtree_depth(const http_json_t *j) {
    unsigned depth=1;
    for(const http_json_t *c=j->child;c;c=c->next) {
        unsigned child=1+subtree_depth(c);
        if(child>depth)depth=child;
    }
    return depth;
}
bool http_json_attach(http_body_t *b, http_json_t *parent, const char *key, http_json_t *j) {
    http_json_t **p, *old = NULL, *cursor;
    unsigned total, old_count = 0, depth = 0;
    if (!parent || (parent->type != 4 && parent->type != 5))
        return false;
    for (cursor = parent; cursor; cursor = cursor->parent)
        if (++depth >= 16)
            return false;
    if(depth+subtree_depth(j)>16)return false;
    if (parent->type == 4) {
        old = child_key(parent, key);
        if (old)
            old_count = http_json_count(old);
        j->key = copy_text(key, strlen(key));
        if (!j->key)
            return false;
    }
    total = http_json_count(j);
    if (b->nodes + total - old_count > HTTP_NODE_LIMIT)
        return false;
    if (old) {
        for (cursor = b->current; cursor; cursor = cursor->parent)
            if (cursor == old) {
                b->current = parent;
                break;
            }
        detach(old);
        http_json_free(old);
    }
    p = &parent->child;
    while (*p)
        p = &(*p)->next;
    *p = j;
    j->parent = parent;
    b->nodes += total - old_count;
    return true;
}
http_body_t *http_body_new(uint8_t format) {
    http_body_t *b;
    if (format < 1 || format > 4)
        return NULL;
    b = calloc(1, sizeof(*b));
    if (!b)
        return NULL;
    b->format = format;
    if (format != 1) {
        b->root = http_json_new(format == 3 ? 5 : 4);
        if (!b->root) {
            free(b);
            return NULL;
        }
        b->current = b->root;
        b->nodes = 1;
    }
    return b;
}
void http_body_free(http_body_t *b) {
    if (!b)
        return;
    http_json_free(b->root);
    free(b->binary);
    free(b);
}
bool http_body_clear(http_body_t *b) {
    http_json_t *root = NULL;
    if (b->format != 1) {
        root = http_json_new(b->format == 3 ? 5 : 4);
        if (!root)
            return false;
    }
    http_json_free(b->root);
    free(b->binary);
    b->root = b->current = root;
    b->binary = NULL;
    b->length = 0;
    b->nodes = root ? 1 : 0;
    return true;
}
http_json_t *http_json_path(http_json_t *j, const char *path) {
    char key[256];
    while (*path) {
        size_t n = 0;
        while (*path && *path != '/' && *path != '%' && *path != '[') {
            if (n == 255)
                return NULL;
            key[n++] = *path++;
        }
        key[n] = 0;
        if (n) {
            if (!j || j->type != 4)
                return NULL;
            j = child_key(j, key);
            if (!j)
                return NULL;
        }
        if (*path == '%' || *path == '[') {
            bool bracket = *path++ == '[';
            unsigned index = 0, count = 0;
            if (!j || j->type != 5)
                return NULL;
            while (isdigit((unsigned char)*path)) {
                if (++count > 5)
                    return NULL;
                index = index * 10 + (unsigned)(*path++ - '0');
            }
            if (!count)
                return NULL;
            if (bracket && *path++ != ']')
                return NULL;
            j = j->child;
            while (j && index--)
                j = j->next;
            if (!j)
                return NULL;
        }
        if (*path) {
            if (*path++ != '/')
                return NULL;
            if (!*path)
                return NULL;
        }
    }
    return j;
}
http_json_t *http_json_decode(const uint8_t *p, size_t size, size_t *pos, unsigned depth,
                              unsigned *count) {
    http_json_t *j;
    uint8_t type;
    unsigned entries, i;
    if (depth >= 16 || *pos >= size || ++*count > HTTP_NODE_LIMIT)
        return NULL;
    type = p[(*pos)++];
    if (type < 1 || type > 5)
        return NULL;
    j = http_json_new(type);
    if (!j)
        return NULL;
    switch (type) {
    case 1:
        if (size - *pos < 4)
            goto fail;
        j->integer = (int32_t)((uint32_t)p[*pos] | ((uint32_t)p[*pos + 1] << 8) |
                               ((uint32_t)p[*pos + 2] << 16) | ((uint32_t)p[*pos + 3] << 24));
        *pos += 4;
        break;
    case 2:
        if (*pos >= size)
            goto fail;
        j->integer = p[(*pos)++] != 0;
        break;
    case 3:
        if (*pos >= size)
            goto fail;
        entries = p[(*pos)++];
        if (size - *pos < entries || memchr(p + *pos, 0, entries))
            goto fail;
        j->text = copy_text(p + *pos, entries);
        *pos += entries;
        if (!j->text)
            goto fail;
        break;
    default:
        if (*pos >= size)
            goto fail;
        entries = p[(*pos)++];
        {
            http_json_t **tail = &j->child;
            for (i = 0; i < entries; i++) {
                char *key = NULL;
                http_json_t *c;
                if (type == 4) {
                    unsigned len;
                    if (*pos >= size)
                        goto fail;
                    len = p[(*pos)++];
                    if (size - *pos < len || memchr(p + *pos, 0, len))
                        goto fail;
                    key = copy_text(p + *pos, len);
                    *pos += len;
                    if (!key)
                        goto fail;
                }
                c = http_json_decode(p, size, pos, depth + 1, count);
                if (!c) {
                    free(key);
                    goto fail;
                }
                c->key = key;
                c->parent = j;
                *tail = c;
                tail = &c->next;
            }
        }
        break;
    }
    return j;
fail:
    http_json_free(j);
    return NULL;
}
typedef struct {
    uint8_t *data;
    size_t length;
    bool failed;
} output_t;
static void emit(output_t *o, const void *p, size_t size) {
    if (o->failed)
        return;
    if (size > HTTP_BODY_LIMIT - o->length) {
        o->failed = true;
        return;
    }
    memcpy(o->data + o->length, p, size);
    o->length += size;
}
static void byte(output_t *o, uint8_t v) { emit(o, &v, 1); }
static void encode(output_t *o, http_json_t *j, unsigned depth) {
    http_json_t *c;
    size_t len;
    unsigned count = 0;
    if (depth >= 16) {
        o->failed = true;
        return;
    }
    byte(o, j->type);
    switch (j->type) {
    case 1: {
        uint32_t v = (uint32_t)j->integer;
        unsigned i;
        for (i = 0; i < 4; i++) {
            byte(o, (uint8_t)v);
            v >>= 8;
        }
        break;
    }
    case 2:
        byte(o, j->integer ? 1 : 0);
        break;
    case 3:
        len = strlen(j->text);
        if (len > 255) {
            o->failed = true;
            break;
        }
        byte(o, (uint8_t)len);
        emit(o, j->text, len);
        break;
    case 4:
    case 5:
        for (c = j->child; c; c = c->next)
            count++;
        if (count > 255) {
            o->failed = true;
            break;
        }
        byte(o, (uint8_t)count);
        for (c = j->child; c; c = c->next) {
            if (j->type == 4) {
                len = strlen(c->key);
                if (len > 255) {
                    o->failed = true;
                    return;
                }
                byte(o, (uint8_t)len);
                emit(o, c->key, len);
            }
            encode(o, c, depth + 1);
        }
        break;
    default:
        o->failed = true;
        break;
    }
}
uint8_t *http_json_encode(http_json_t *j, size_t *size) {
    output_t o = {malloc(HTTP_BODY_LIMIT), 0, false};
    if (!o.data)
        return NULL;
    encode(&o, j, 0);
    if (o.failed) {
        free(o.data);
        return NULL;
    }
    *size = o.length;
    return o.data;
}
static void quote(output_t *o, const char *s) {
    byte(o, '"');
    while (*s) {
        unsigned char c = (unsigned char)*s++;
        char escaped[7];
        if (c == '"' || c == '\\') {
            byte(o, '\\');
            byte(o, c);
        } else if (c < 32) {
            snprintf(escaped, sizeof(escaped), "\\u%04x", c);
            emit(o, escaped, 6);
        } else
            byte(o, c);
    }
    byte(o, '"');
}
static void render(output_t *o, http_json_t *j, unsigned depth) {
    http_json_t *c;
    char number[24];
    bool comma = false;
    if (depth >= 16) {
        o->failed = true;
        return;
    }
    switch (j->type) {
    case 1:
        snprintf(number, sizeof(number), "%ld", (long)j->integer);
        emit(o, number, strlen(number));
        break;
    case 2:
        emit(o, j->integer ? "true" : "false", j->integer ? 4 : 5);
        break;
    case 3:
        quote(o, j->text);
        break;
    case 4:
    case 5:
        byte(o, j->type == 4 ? '{' : '[');
        for (c = j->child; c; c = c->next) {
            if (comma)
                byte(o, ',');
            comma = true;
            if (j->type == 4) {
                quote(o, c->key);
                byte(o, ':');
            }
            render(o, c, depth + 1);
        }
        byte(o, j->type == 4 ? '}' : ']');
        break;
    case 6:
        emit(o, j->text, strlen(j->text));
        break;
    case 7:
        emit(o, "null", 4);
        break;
    default:
        o->failed = true;
        break;
    }
}
static void url_quote(output_t *o, const uint8_t *p, size_t size) {
    static const char hex[] = "0123456789ABCDEF";
    while (size--) {
        unsigned char c = *p++;
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
            byte(o, c);
        else {
            byte(o, '%');
            byte(o, (uint8_t)hex[c >> 4]);
            byte(o, (uint8_t)hex[c & 15]);
        }
    }
}
uint8_t *http_json_render(http_json_t *j, bool url, size_t *size) {
    output_t o = {malloc(HTTP_BODY_LIMIT), 0, false};
    if (!o.data)
        return NULL;
    if (!url)
        render(&o, j, 0);
    else {
        http_json_t *c;
        bool amp = false;
        if (j->type != 4)
            o.failed = true;
        for (c = j->child; c && !o.failed; c = c->next) {
            output_t tmp = {malloc(HTTP_BODY_LIMIT), 0, false};
            if (!tmp.data) {
                o.failed = true;
                break;
            }
            if (amp)
                byte(&o, '&');
            amp = true;
            url_quote(&o, (const uint8_t *)c->key, strlen(c->key));
            byte(&o, '=');
            if (c->type == 3)
                emit(&tmp, c->text, strlen(c->text));
            else
                render(&tmp, c, 0);
            if (tmp.failed)
                o.failed = true;
            else
                url_quote(&o, tmp.data, tmp.length);
            free(tmp.data);
        }
    }
    byte(&o, '\n');
    if (o.failed) {
        free(o.data);
        return NULL;
    }
    *size = o.length;
    return o.data;
}
typedef struct {
    const char *p;
    size_t size, pos;
    unsigned nodes;
} parser_t;
static void space(parser_t *p) {
    while (p->pos < p->size && strchr(" \r\n\t", p->p[p->pos]))
        p->pos++;
}
static int hex(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}
static int unicode(parser_t *p) {
    unsigned i, v = 0;
    for (i = 0; i < 4; i++) {
        int h;
        if (p->pos >= p->size || (h = hex(p->p[p->pos++])) < 0)
            return -1;
        v = v * 16 + (unsigned)h;
    }
    return (int)v;
}
static char *parse_string(parser_t *p) {
    char *s;
    size_t at = 0, end;
    if (p->pos >= p->size || p->p[p->pos++] != '"')
        return NULL;
    /* Bound storage by this encoded token, not the remaining document.
     * JSON escapes can only shrink when decoded to UTF-8. Skip escaped
     * quotes here; the decoder below still validates every escape. */
    for (end = p->pos; end < p->size; end++) {
        if (p->p[end] == '"')
            break;
        if (p->p[end] == '\\') {
            if (++end == p->size)
                return NULL;
        }
    }
    if (end == p->size)
        return NULL;
    s = malloc(end - p->pos + 1);
    if (!s)
        return NULL;
    while (p->pos < p->size) {
        unsigned c = (unsigned char)p->p[p->pos++];
        if (c == '"') {
            s[at] = 0;
            return s;
        }
        if (c < 32)
            break;
        if (c == '\\') {
            if (p->pos >= p->size)
                break;
            c = (unsigned char)p->p[p->pos++];
            if (c == 'u') {
                int code = unicode(p);
                if (code < 0 || code == 0)
                    break;
                if (code >= 0xd800 && code <= 0xdbff) {
                    int low;
                    if (p->size - p->pos < 6 || p->p[p->pos++] != '\\' || p->p[p->pos++] != 'u')
                        break;
                    low = unicode(p);
                    if (low < 0xdc00 || low > 0xdfff)
                        break;
                    code = 0x10000 + ((code - 0xd800) << 10) + (low - 0xdc00);
                } else if (code >= 0xdc00 && code <= 0xdfff)
                    break;
                if (code < 0x80)
                    s[at++] = (char)code;
                else if (code < 0x800) {
                    s[at++] = (char)(0xc0 | (code >> 6));
                    s[at++] = (char)(0x80 | (code & 63));
                } else if (code < 0x10000) {
                    s[at++] = (char)(0xe0 | (code >> 12));
                    s[at++] = (char)(0x80 | ((code >> 6) & 63));
                    s[at++] = (char)(0x80 | (code & 63));
                } else {
                    s[at++] = (char)(0xf0 | (code >> 18));
                    s[at++] = (char)(0x80 | ((code >> 12) & 63));
                    s[at++] = (char)(0x80 | ((code >> 6) & 63));
                    s[at++] = (char)(0x80 | (code & 63));
                }
                continue;
            } else if (c == 'n')
                c = '\n';
            else if (c == 'r')
                c = '\r';
            else if (c == 't')
                c = '\t';
            else if (c == 'b')
                c = '\b';
            else if (c == 'f')
                c = '\f';
            else if (c != '"' && c != '\\' && c != '/')
                break;
        }
        s[at++] = (char)c;
    }
    free(s);
    return NULL;
}
static http_json_t *parse_value(parser_t *p, unsigned depth) {
    http_json_t *j = NULL;
    char ch;
    space(p);
    if (p->pos >= p->size || depth >= 16 || ++p->nodes > HTTP_NODE_LIMIT)
        return NULL;
    ch = p->p[p->pos];
    if (ch == '{' || ch == '[') {
        http_json_t **tail;
        bool object = ch == '{';
        p->pos++;
        j = http_json_new(object ? 4 : 5);
        if (!j)
            return NULL;
        tail = &j->child;
        space(p);
        if (p->pos < p->size && p->p[p->pos] == (object ? '}' : ']')) {
            p->pos++;
            return j;
        }
        for (;;) {
            char *key = NULL;
            http_json_t *c;
            if (object) {
                space(p);
                key = parse_string(p);
                if (!key)
                    goto fail;
                space(p);
                if (p->pos >= p->size || p->p[p->pos++] != ':') {
                    free(key);
                    goto fail;
                }
            }
            c = parse_value(p, depth + 1);
            if (!c) {
                free(key);
                goto fail;
            }
            c->key = key;
            c->parent = j;
            *tail = c;
            tail = &c->next;
            space(p);
            if (p->pos >= p->size)
                goto fail;
            ch = p->p[p->pos++];
            if (ch == (object ? '}' : ']'))
                return j;
            if (ch != ',')
                goto fail;
        }
    }
    if (ch == '"') {
        j = http_json_new(3);
        if (!j)
            return NULL;
        j->text = parse_string(p);
        if (!j->text)
            goto fail;
        return j;
    }
    if (p->size - p->pos >= 4 && !memcmp(p->p + p->pos, "true", 4)) {
        j = http_json_new(2);
        if (j)
            j->integer = 1;
        p->pos += 4;
        return j;
    }
    if (p->size - p->pos >= 5 && !memcmp(p->p + p->pos, "false", 5)) {
        p->pos += 5;
        return http_json_new(2);
    }
    if (p->size - p->pos >= 4 && !memcmp(p->p + p->pos, "null", 4)) {
        p->pos += 4;
        return http_json_new(7);
    }
    {
        size_t start = p->pos;
        bool fraction = false;
        char *end;
        long long value;
        if (ch == '-')
            p->pos++;
        if (p->pos >= p->size || !isdigit((unsigned char)p->p[p->pos]))
            return NULL;
        if (p->p[p->pos] == '0')
            p->pos++;
        else
            while (p->pos < p->size && isdigit((unsigned char)p->p[p->pos]))
                p->pos++;
        if (p->pos < p->size && p->p[p->pos] == '.') {
            fraction = true;
            p->pos++;
            if (p->pos >= p->size || !isdigit((unsigned char)p->p[p->pos]))
                return NULL;
            while (p->pos < p->size && isdigit((unsigned char)p->p[p->pos]))
                p->pos++;
        }
        if (p->pos < p->size && (p->p[p->pos] == 'e' || p->p[p->pos] == 'E')) {
            fraction = true;
            p->pos++;
            if (p->pos < p->size && (p->p[p->pos] == '+' || p->p[p->pos] == '-'))
                p->pos++;
            if (p->pos >= p->size || !isdigit((unsigned char)p->p[p->pos]))
                return NULL;
            while (p->pos < p->size && isdigit((unsigned char)p->p[p->pos]))
                p->pos++;
        }
        j = http_json_new(6);
        if (!j)
            return NULL;
        j->text = copy_text(p->p + start, p->pos - start);
        if (!j->text)
            goto fail;
        value = strtoll(j->text, &end, 10);
        if (!fraction && !*end && value >= INT32_MIN && value <= INT32_MAX) {
            j->type = 1;
            j->integer = (int32_t)value;
            free(j->text);
            j->text = NULL;
        }
        return j;
    }
fail:
    http_json_free(j);
    return NULL;
}
http_json_t *http_json_parse(const char *p, size_t size) {
    parser_t parser = {p, size, 0, 0};
    http_json_t *j;
    if (size > HTTP_BODY_LIMIT || memchr(p, 0, size))
        return NULL;
    j = parse_value(&parser, 0);
    space(&parser);
    if (parser.pos != size) {
        http_json_free(j);
        return NULL;
    }
    return j;
}
bool http_body_command(http_body_t *b, uint8_t cmd, const uint8_t *p, size_t size, uint8_t **reply,
                       size_t *reply_size) {
    http_json_t *j = NULL;
    char key[256], path[896];
    size_t pos = 0;
    unsigned count = 0;
    *reply = NULL;
    *reply_size = 0;
    if (cmd == 0x2e)
        return !size && http_body_clear(b);
    if (cmd == 0x2c) {
        uint8_t *new_data;
        if (b->format != 1 || size > HTTP_BODY_LIMIT - b->length)
            return false;
        new_data = realloc(b->binary, b->length + size + 1);
        if (!new_data)
            return false;
        b->binary = new_data;
        memcpy(new_data + b->length, p, size);
        b->length += size;
        return true;
    }
    if (b->format == 1)
        return false;
    if (cmd == 0x28) {
        if (size)
            return false;
        if (b->current->parent)
            b->current = b->current->parent;
        return true;
    }
    if (cmd >= 0x29 && cmd <= 0x2b) {
        if (size >= sizeof(path))
            return false;
        memcpy(path, p, size);
        path[size] = 0;
        j = http_json_path(b->root, path);
        if (!j)
            return false;
        if (cmd == 0x2a) {
            *reply = http_json_encode(j, reply_size);
            return *reply != NULL;
        }
        if (cmd == 0x2b) {
            b->current = j;
            return true;
        }
        if (j != b->root) {
            http_json_t *cursor;
            for (cursor = b->current; cursor; cursor = cursor->parent)
                if (cursor == j) {
                    b->current = j->parent;
                    break;
                }
            b->nodes -= http_json_count(j);
            detach(j);
            http_json_free(j);
        }
        return true;
    }
    if (cmd == 0x2d) {
        while (pos < size) {
            unsigned len = 0;
            if (b->current->type == 4) {
                len = p[pos++];
                if (size - pos < len || memchr(p + pos, 0, len))
                    return false;
                memcpy(key, p + pos, len);
                pos += len;
            }
            key[len] = 0;
            j = http_json_decode(p, size, &pos, 0, &count);
            if (!j)
                return false;
            if (!http_json_attach(b, b->current, key, j)) {
                http_json_free(j);
                return false;
            }
        }
        return true;
    }
    if (cmd < 0x23 || cmd > 0x27 || !size)
        return false;
    pos = 1 + p[0];
    if (pos > size || memchr(p + 1, 0, p[0]))
        return false;
    memcpy(key, p + 1, p[0]);
    key[p[0]] = 0;
    j = http_json_new((uint8_t)(cmd - 0x22));
    if (!j)
        return false;
    if (cmd == 0x23) {
        size_t len = size - pos, i;
        uint32_t value;
        if (!len || len > 4)
            goto bad;
        value = p[size - 1] & 128 ? UINT32_MAX : 0;
        for (i = len; i; i--)
            value = (value << 8) | p[pos + i - 1];
        j->integer = (int32_t)value;
    } else if (cmd == 0x24) {
        if (size - pos != 1)
            goto bad;
        j->integer = p[pos] != 0;
    } else if (cmd == 0x25) {
        if (pos >= size || size - pos - 1 != p[pos] || memchr(p + pos + 1, 0, p[pos]))
            goto bad;
        j->text = copy_text(p + pos + 1, p[pos]);
        if (!j->text)
            goto bad;
    } else if (pos != size)
        goto bad;
    if (!http_json_attach(b, b->current, key, j))
        goto bad;
    if (cmd == 0x26 || cmd == 0x27)
        b->current = j;
    return true;
bad:
    http_json_free(j);
    return false;
}
