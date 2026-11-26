#include "backend.h"

#include "xmpp.h"

#include <nappgui.h>
#include <sewer/bstd.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#include <direct.h>
#define CAL_CREATE_DIR(path) _mkdir(path)
#else
#include <sys/stat.h>
#define CAL_CREATE_DIR(path) mkdir(path, 0700)
#endif

#define CAL_MAX_BUDDIES CALANTAS_MAX_BUDDIES
#define CAL_MAX_PENDING 128
#define CAL_LINE_MAX 2048
#define CAL_FIELD_MAX CALANTAS_MAX_TEXT

typedef struct _account_store_t
{
    CalAccount accounts[CALANTAS_MAX_ACCOUNTS];
    uint32_t count;
    char_t active[64];
} AccountStore;

struct _backend_t
{
    CalBuddy buddies[CAL_MAX_BUDDIES];
    uint32_t count;
    cal_status_t status;
    char_t status_text[128];
    char_t account_name[64];
    char_t account_marker[64];
    char_t jid[128];
    char_t server[128];
    char_t password[128];
    char_t connection_text[256];
    bool_t auto_connect;
    XmppBackend *xmpp;
    CalEvent events[CAL_MAX_PENDING];
    uint32_t event_head;
    uint32_t event_tail;
};

static bool_t i_load_account_store(AccountStore *store);
static bool_t i_save_account_store(const AccountStore *store);

static void i_init_buddy(CalBuddy *buddy, const char_t *name, const char_t *handle,
                         const cal_status_t status, const char_t *status_text)
{
    bstd_sprintf(buddy->name, sizeof(buddy->name), "%s", name != NULL ? name : "");
    bstd_sprintf(buddy->handle, sizeof(buddy->handle), "%s", handle != NULL ? handle : "");
    buddy->status = status;
    bstd_sprintf(buddy->status_text, sizeof(buddy->status_text), "%s",
                 status_text != NULL ? status_text : "");
    buddy->message_count = 0;
}

static void i_add_message(Backend *backend, const uint32_t index, const bool_t own,
                          const char_t *text)
{
    CalBuddy *buddy;
    CalMessage *message;
    char_t stamp[16];
    time_t now;
    struct tm *local;

    if (backend == NULL || index >= backend->count || text == NULL || text[0] == '\0')
        return;

    buddy = &backend->buddies[index];
    if (buddy->message_count >= CALANTAS_MAX_MESSAGES)
        return;

    now = time(NULL);
    local = localtime(&now);
    if (local != NULL)
        bstd_sprintf(stamp, sizeof(stamp), "%02d:%02d", local->tm_hour, local->tm_min);
    else
        bstd_sprintf(stamp, sizeof(stamp), "%s", "--:--");

    message = &buddy->messages[buddy->message_count];
    bstd_sprintf(message->time, sizeof(message->time), "%s", stamp);
    bstd_sprintf(message->sender, sizeof(message->sender), "%s", own ? "me" : buddy->name);
    bstd_sprintf(message->text, sizeof(message->text), "%s", text);
    message->own = own;
    buddy->message_count += 1;
}

static void i_push_event(Backend *backend, const uint32_t buddy,
                         const cal_event_type_t type, const char_t *text)
{
    if ((backend->event_tail + 1) % CAL_MAX_PENDING == backend->event_head)
        return;

    backend->events[backend->event_tail].type = type;
    backend->events[backend->event_tail].buddy = buddy;
    bstd_sprintf(backend->events[backend->event_tail].text,
                 sizeof(backend->events[backend->event_tail].text), "%s",
                 text != NULL ? text : "");
    backend->event_tail = (backend->event_tail + 1) % CAL_MAX_PENDING;
}

static void i_bare_handle(char_t *out, const uint32_t size, const char_t *handle)
{
    uint32_t i = 0;

    if (out == NULL || size == 0)
        return;
    if (handle == NULL)
    {
        out[0] = '\0';
        return;
    }

    while (handle[i] != '\0' && handle[i] != '/' && i + 1 < size)
    {
        out[i] = handle[i];
        i += 1;
    }
    out[i] = '\0';
}

static bool_t i_same_handle(const char_t *left, const char_t *right)
{
    size_t i;

    if (left == NULL || right == NULL)
        return FALSE;

    for (i = 0; left[i] != '\0' && right[i] != '\0'; i += 1)
    {
        if (tolower((unsigned char)left[i]) != tolower((unsigned char)right[i]))
            return FALSE;
    }

    return left[i] == '\0' && right[i] == '\0';
}

static uint32_t i_find_buddy(const Backend *backend, const char_t *handle)
{
    char_t wanted[128];
    char_t current[128];
    uint32_t i;

    if (backend == NULL || handle == NULL || handle[0] == '\0')
        return CALANTAS_MAX_BUDDIES;

    i_bare_handle(wanted, sizeof(wanted), handle);
    for (i = 0; i < backend->count; i += 1)
    {
        i_bare_handle(current, sizeof(current), backend->buddies[i].handle);
        if (i_same_handle(current, wanted) == TRUE)
            return i;
    }
    return CALANTAS_MAX_BUDDIES;
}

static uint32_t i_contact(Backend *backend, const char_t *name, const char_t *jid,
                          const cal_status_t status, const char_t *status_text)
{
    uint32_t index = i_find_buddy(backend, jid);

    if (index == CALANTAS_MAX_BUDDIES)
    {
        if (backend->count >= CAL_MAX_BUDDIES)
            return CALANTAS_MAX_BUDDIES;
        index = backend->count;
        backend->count += 1;
        i_init_buddy(&backend->buddies[index], name, jid, status, status_text);
    }
    else
    {
        if (name != NULL && name[0] != '\0' &&
            strcmp(backend->buddies[index].name, backend->buddies[index].handle) == 0)
            bstd_sprintf(backend->buddies[index].name, sizeof(backend->buddies[index].name), "%s", name);
        backend->buddies[index].status = status;
        bstd_sprintf(backend->buddies[index].status_text,
                     sizeof(backend->buddies[index].status_text), "%s",
                     status_text != NULL ? status_text : "");
    }
    return index;
}

static void i_poll_xmpp_events(Backend *backend)
{
    CalXmppEvent event;

    while (cal_xmpp_pop_event(backend->xmpp, &event) == TRUE)
    {
        if (event.type == ekCAL_XMPP_EVENT_CONTACT)
        {
            uint32_t index = i_contact(backend, event.name, event.jid, event.status, "");
            if (index < CALANTAS_MAX_BUDDIES)
                i_push_event(backend, index, ekCAL_EVENT_CONTACTS, "Contact list updated");
        }
        else if (event.type == ekCAL_XMPP_EVENT_PRESENCE)
        {
            uint32_t index = i_contact(backend, "", event.jid, event.status, event.text);
            if (index < CALANTAS_MAX_BUDDIES)
                i_push_event(backend, index, ekCAL_EVENT_CONTACTS, "Presence updated");
        }
        else if (event.type == ekCAL_XMPP_EVENT_MESSAGE)
        {
            uint32_t index = i_contact(backend, "", event.jid, ekCAL_AVAILABLE, "");
            if (index < CALANTAS_MAX_BUDDIES)
            {
                i_add_message(backend, index, FALSE, event.text);
                i_push_event(backend, index, ekCAL_EVENT_MESSAGE, "Message received");
            }
        }
        else if (event.type == ekCAL_XMPP_EVENT_CONNECTION)
        {
            bstd_sprintf(backend->connection_text, sizeof(backend->connection_text), "%s", event.text);
            i_push_event(backend, 0, ekCAL_EVENT_CONNECTION, event.text);
        }
    }
}

static bool_t i_config_dir(char_t *out, const uint32_t size)
{
#if defined(_WIN32)
    const char_t *base = getenv("APPDATA");
    if (base == NULL || base[0] == '\0')
        base = getenv("USERPROFILE");
    if (base == NULL || base[0] == '\0')
        return FALSE;
    bstd_sprintf(out, size, "%s\\Calantas", base);
#else
    const char_t *home = getenv("HOME");
    char_t parent[1024];
    if (home == NULL || home[0] == '\0')
        return FALSE;
    bstd_sprintf(parent, sizeof(parent), "%s/.config", home);
    CAL_CREATE_DIR(parent);
    bstd_sprintf(out, size, "%s/.config/calantas", home);
#endif
    CAL_CREATE_DIR(out);
    return TRUE;
}

static bool_t i_config_path(char_t *out, const uint32_t size)
{
    char_t dir[1024];

    if (i_config_dir(dir, sizeof(dir)) == FALSE)
        return FALSE;
#if defined(_WIN32)
    bstd_sprintf(out, size, "%s\\state-xmpp.txt", dir);
#else
    bstd_sprintf(out, size, "%s/state-xmpp.txt", dir);
#endif
    return TRUE;
}

static bool_t i_account_path(char_t *out, const uint32_t size)
{
    char_t dir[1024];

    if (i_config_dir(dir, sizeof(dir)) == FALSE)
        return FALSE;
#if defined(_WIN32)
    bstd_sprintf(out, size, "%s\\account.json", dir);
#else
    bstd_sprintf(out, size, "%s/account.json", dir);
#endif
    return TRUE;
}

static void i_safe_marker(char_t *out, const uint32_t size, const char_t *marker)
{
    uint32_t n = 0;

    if (out == NULL || size == 0)
        return;
    if (marker == NULL || marker[0] == '\0')
    {
        out[0] = '\0';
        return;
    }

    while (*marker != '\0' && n + 1 < size)
    {
        char_t c = *marker;

        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
            || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-')
            out[n++] = c;
        else
            out[n++] = '_';
        marker += 1;
    }
    out[n] = '\0';
}

static bool_t i_state_path(char_t *out, const uint32_t size, const char_t *marker)
{
    char_t dir[1024];
    char_t safe[64];

    if (i_config_dir(dir, sizeof(dir)) == FALSE)
        return FALSE;
    i_safe_marker(safe, sizeof(safe), marker);
#if defined(_WIN32)
    if (safe[0] == '\0' || strcmp(safe, "XMPP") == 0)
        bstd_sprintf(out, size, "%s\\state-xmpp.txt", dir);
    else
        bstd_sprintf(out, size, "%s\\state-xmpp-%s.txt", dir, safe);
#else
    if (safe[0] == '\0' || strcmp(safe, "XMPP") == 0)
        bstd_sprintf(out, size, "%s/state-xmpp.txt", dir);
    else
        bstd_sprintf(out, size, "%s/state-xmpp-%s.txt", dir, safe);
#endif
    return TRUE;
}

static void i_write_escaped(FILE *file, const char_t *text)
{
    const char_t *p;

    if (text == NULL)
        return;

    for (p = text; *p != '\0'; p += 1)
    {
        if (*p == '\\' || *p == '|')
        {
            fputc('\\', file);
            fputc(*p, file);
        }
        else if (*p == '\n')
        {
            fputs("\\n", file);
        }
        else if (*p == '\r')
        {
            fputs("\\r", file);
        }
        else
        {
            fputc(*p, file);
        }
    }
}

static void i_unescape(const char_t *src, char_t *dst, const uint32_t size)
{
    uint32_t n = 0;

    while (*src != '\0' && n + 1 < size)
    {
        char_t c = *src;

        if (c == '\\' && src[1] != '\0')
        {
            src += 1;
            if (*src == 'n')
                c = '\n';
            else if (*src == 'r')
                c = '\r';
            else
                c = *src;
        }

        dst[n++] = c;
        src += 1;
    }

    dst[n] = '\0';
}

static uint32_t i_split_fields(const char_t *line, char_t fields[][CAL_FIELD_MAX],
                               const uint32_t max)
{
    char_t token[CAL_LINE_MAX];
    uint32_t n = 0;
    uint32_t count = 0;
    const char_t *p;

    for (p = line; *p != '\0'; p += 1)
    {
        if (*p == '\\' && p[1] != '\0' && n + 2 < sizeof(token))
        {
            token[n++] = *p;
            p += 1;
            token[n++] = *p;
        }
        else if (*p == '|')
        {
            token[n] = '\0';
            if (count < max)
                i_unescape(token, fields[count], CAL_FIELD_MAX);
            count += 1;
            n = 0;
        }
        else if (n + 1 < sizeof(token))
        {
            token[n++] = *p;
        }
    }

    token[n] = '\0';
    if (count < max)
        i_unescape(token, fields[count], CAL_FIELD_MAX);
    count += 1;
    return count;
}

static void i_strip_eol(char_t *line)
{
    size_t n = strlen(line);

    while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r'))
        n -= 1;
    line[n] = '\0';
}

static uint32_t i_json_hex4(const char_t *text)
{
    uint32_t value = 0;
    uint32_t i;

    for (i = 0; i < 4; i += 1)
    {
        char_t c = text[i];

        value <<= 4;
        if (c >= '0' && c <= '9')
            value += (uint32_t)(c - '0');
        else if (c >= 'a' && c <= 'f')
            value += (uint32_t)(c - 'a') + 10u;
        else if (c >= 'A' && c <= 'F')
            value += (uint32_t)(c - 'A') + 10u;
    }
    return value;
}

static uint32_t i_utf8_append(const uint32_t code, char_t *out, uint32_t n, const uint32_t size)
{
    if (code < 0x80u)
    {
        if (n + 1 < size)
            out[n++] = (char_t)code;
    }
    else if (code < 0x800u)
    {
        if (n + 2 < size)
        {
            out[n++] = (char_t)(0xC0u | (code >> 6));
            out[n++] = (char_t)(0x80u | (code & 0x3Fu));
        }
    }
    else if (code < 0x10000u)
    {
        if (n + 3 < size)
        {
            out[n++] = (char_t)(0xE0u | (code >> 12));
            out[n++] = (char_t)(0x80u | ((code >> 6) & 0x3Fu));
            out[n++] = (char_t)(0x80u | (code & 0x3Fu));
        }
    }
    else if (code <= 0x10FFFFu && n + 4 < size)
    {
        out[n++] = (char_t)(0xF0u | (code >> 18));
        out[n++] = (char_t)(0x80u | ((code >> 12) & 0x3Fu));
        out[n++] = (char_t)(0x80u | ((code >> 6) & 0x3Fu));
        out[n++] = (char_t)(0x80u | (code & 0x3Fu));
    }
    return n;
}

static void i_json_unescape(const char_t *src, char_t *out, const uint32_t size)
{
    uint32_t n = 0;

    while (*src != '\0' && n + 1 < size)
    {
        if (*src != '\\')
        {
            out[n++] = *src;
            src += 1;
            continue;
        }

        src += 1;
        if (*src == '\0')
            break;

        if (*src == 'b')
        {
            out[n++] = '\b';
            src += 1;
        }
        else if (*src == 'f')
        {
            out[n++] = '\f';
            src += 1;
        }
        else if (*src == 'n')
        {
            out[n++] = '\n';
            src += 1;
        }
        else if (*src == 'r')
        {
            out[n++] = '\r';
            src += 1;
        }
        else if (*src == 't')
        {
            out[n++] = '\t';
            src += 1;
        }
        else if (*src == 'u' && strlen(src) >= 5)
        {
            uint32_t code = i_json_hex4(src + 1);

            src += 5;
            if (code >= 0xD800u && code <= 0xDBFFu && src[0] == '\\' && src[1] == 'u'
                && strlen(src) >= 6)
            {
                uint32_t low = i_json_hex4(src + 2);

                if (low >= 0xDC00u && low <= 0xDFFFu)
                {
                    code = 0x10000u + ((code - 0xD800u) << 10) + (low - 0xDC00u);
                    src += 6;
                }
            }
            n = i_utf8_append(code, out, n, size);
        }
        else
        {
            out[n++] = *src;
            src += 1;
        }
    }

    out[n] = '\0';
}

static void i_json_write_string(FILE *file, const char_t *text)
{
    const char_t *p;

    fputc('"', file);
    if (text != NULL)
    {
        for (p = text; *p != '\0'; p += 1)
        {
            unsigned char c = (unsigned char)*p;

            if (c == '"')
                fputs("\\\"", file);
            else if (c == '\\')
                fputs("\\\\", file);
            else if (c == '\n')
                fputs("\\n", file);
            else if (c == '\r')
                fputs("\\r", file);
            else if (c == '\t')
                fputs("\\t", file);
            else if (c < 0x20u)
                fprintf(file, "\\u%04x", (unsigned)c);
            else
                fputc((int)c, file);
        }
    }
    fputc('"', file);
}

static const char_t *i_json_read_key(const char_t *line, const char_t *key)
{
    char_t found[64];
    uint32_t n = 0;
    const char_t *p = line;

    while (*p == ' ' || *p == '\t')
        p += 1;
    if (*p != '"')
        return NULL;
    p += 1;
    while (*p != '\0' && *p != '"' && n + 1 < sizeof(found))
    {
        found[n++] = *p;
        p += 1;
    }
    found[n] = '\0';
    if (*p != '"')
        return NULL;
    p += 1;
    while (*p == ' ' || *p == '\t')
        p += 1;
    if (*p != ':')
        return NULL;
    p += 1;
    while (*p == ' ' || *p == '\t')
        p += 1;
    return strcmp(found, key) == 0 ? p : NULL;
}

static bool_t i_json_read_string(const char_t *line, const char_t *key,
                                 char_t *out, const uint32_t size)
{
    char_t raw[CAL_LINE_MAX];
    uint32_t n = 0;
    const char_t *p = i_json_read_key(line, key);

    if (p == NULL || *p != '"')
        return FALSE;
    p += 1;
    while (*p != '\0' && *p != '"')
    {
        if (*p == '\\' && p[1] != '\0')
        {
            if (n + 2 >= sizeof(raw))
                break;
            raw[n++] = *p;
            raw[n++] = p[1];
            p += 2;
        }
        else
        {
            if (n + 1 >= sizeof(raw))
                break;
            raw[n++] = *p;
            p += 1;
        }
    }
    if (*p != '"')
        return FALSE;
    raw[n] = '\0';
    i_json_unescape(raw, out, size);
    return TRUE;
}

static bool_t i_json_read_bool(const char_t *line, const char_t *key, bool_t *value)
{
    const char_t *p = i_json_read_key(line, key);

    if (p == NULL || value == NULL)
        return FALSE;
    if (strncmp(p, "true", 4) == 0)
        *value = TRUE;
    else if (strncmp(p, "false", 5) == 0)
        *value = FALSE;
    else
        return FALSE;
    return TRUE;
}

static uint32_t i_find_account(const AccountStore *store, const char_t *name)
{
    uint32_t i;

    if (store == NULL || name == NULL)
        return CALANTAS_MAX_ACCOUNTS;
    for (i = 0; i < store->count; i += 1)
    {
        if (strcmp(store->accounts[i].name, name) == 0)
            return i;
    }
    return CALANTAS_MAX_ACCOUNTS;
}

static bool_t i_save_account_store(const AccountStore *store)
{
    char_t path[1200];
    FILE *file;
    uint32_t i;

    if (store == NULL || i_account_path(path, sizeof(path)) == FALSE)
        return FALSE;
    file = fopen(path, "wb");
    if (file == NULL)
        return FALSE;

    fputs("{\n  \"active\": ", file);
    i_json_write_string(file, store->active);
    fputs(",\n  \"accounts\": [\n", file);
    for (i = 0; i < store->count; i += 1)
    {
        const CalAccount *account = &store->accounts[i];

        fputs("    {\n      \"name\": ", file);
        i_json_write_string(file, account->name);
        fputs(",\n      \"jid\": ", file);
        i_json_write_string(file, account->jid);
        fputs(",\n      \"password\": ", file);
        i_json_write_string(file, account->password);
        fputs(",\n      \"server\": ", file);
        i_json_write_string(file, account->server);
        fprintf(file, ",\n      \"auto_connect\": %s\n    }%s\n",
                account->auto_connect == TRUE ? "true" : "false",
                i + 1 < store->count ? "," : "");
    }
    fputs("  ]\n}\n", file);
    fclose(file);
#if !defined(_WIN32)
    chmod(path, 0600);
#endif
    return TRUE;
}

static bool_t i_read_legacy_account(CalAccount *account)
{
    char_t path[1200];
    char_t line[CAL_LINE_MAX];
    char_t fields[8][CAL_FIELD_MAX];
    FILE *file;

    if (account == NULL || i_config_path(path, sizeof(path)) == FALSE)
        return FALSE;
    file = fopen(path, "rb");
    if (file == NULL)
        return FALSE;

    memset(account, 0, sizeof(*account));
    bstd_sprintf(account->name, sizeof(account->name), "%s", "XMPP");
    while (fgets(line, sizeof(line), file) != NULL)
    {
        uint32_t nfields;

        i_strip_eol(line);
        nfields = i_split_fields(line, fields, 8);
        if (nfields < 2)
            continue;
        if (strcmp(fields[0], "C") == 0)
            account->auto_connect = strcmp(fields[1], "1") == 0 ? TRUE : FALSE;
        else if (strcmp(fields[0], "J") == 0)
            bstd_sprintf(account->jid, sizeof(account->jid), "%s", fields[1]);
        else if (strcmp(fields[0], "R") == 0)
            bstd_sprintf(account->server, sizeof(account->server), "%s", fields[1]);
        else if (strcmp(fields[0], "P") == 0)
            bstd_sprintf(account->password, sizeof(account->password), "%s", fields[1]);
    }
    fclose(file);
    return account->jid[0] != '\0' || account->password[0] != '\0';
}

static void i_push_account(AccountStore *store, CalAccount *current, bool_t *have_current)
{
    if (*have_current == TRUE && store->count < CALANTAS_MAX_ACCOUNTS
        && (current->name[0] != '\0' || current->jid[0] != '\0'
            || current->password[0] != '\0' || current->server[0] != '\0'))
        store->accounts[store->count++] = *current;
    memset(current, 0, sizeof(*current));
    *have_current = FALSE;
}

static bool_t i_load_account_store(AccountStore *store)
{
    char_t path[1200];
    char_t line[CAL_LINE_MAX];
    CalAccount current;
    FILE *file;
    bool_t have_current = FALSE;
    bool_t legacy_json = FALSE;
    bool_t changed = FALSE;

    if (store == NULL || i_account_path(path, sizeof(path)) == FALSE)
        return FALSE;
    memset(store, 0, sizeof(*store));
    memset(&current, 0, sizeof(current));

    file = fopen(path, "rb");
    if (file == NULL)
    {
        CalAccount legacy;

        if (i_read_legacy_account(&legacy) == TRUE)
        {
            store->accounts[0] = legacy;
            store->count = 1;
            bstd_sprintf(store->active, sizeof(store->active), "%s", legacy.name);
            changed = TRUE;
        }
    }
    else
    {
        while (fgets(line, sizeof(line), file) != NULL)
        {
            i_strip_eol(line);
            if (i_json_read_string(line, "active", store->active, sizeof(store->active)) == TRUE)
                continue;
            if (i_json_read_string(line, "name", current.name, sizeof(current.name)) == TRUE)
            {
                i_push_account(store, &current, &have_current);
                have_current = TRUE;
                continue;
            }
            if (have_current == FALSE)
            {
                bstd_sprintf(current.name, sizeof(current.name), "%s", "XMPP");
                have_current = TRUE;
                legacy_json = TRUE;
            }
            if (i_json_read_string(line, "jid", current.jid, sizeof(current.jid)) == TRUE)
                continue;
            if (i_json_read_string(line, "password", current.password, sizeof(current.password)) == TRUE)
                continue;
            if (i_json_read_string(line, "server", current.server, sizeof(current.server)) == TRUE)
                continue;
            i_json_read_bool(line, "auto_connect", &current.auto_connect);
        }
        fclose(file);
        i_push_account(store, &current, &have_current);
    }

    if (store->count > 0 && i_find_account(store, store->active) == CALANTAS_MAX_ACCOUNTS)
    {
        bstd_sprintf(store->active, sizeof(store->active), "%s", store->accounts[0].name);
        changed = TRUE;
    }

    if (store->count > 0 && legacy_json == TRUE
        && (store->accounts[0].jid[0] == '\0' || store->accounts[0].password[0] == '\0'))
    {
        CalAccount legacy;

        if (i_read_legacy_account(&legacy) == TRUE)
        {
            if (store->accounts[0].jid[0] == '\0')
                bstd_sprintf(store->accounts[0].jid, sizeof(store->accounts[0].jid), "%s", legacy.jid);
            if (store->accounts[0].password[0] == '\0')
                bstd_sprintf(store->accounts[0].password, sizeof(store->accounts[0].password), "%s", legacy.password);
            if (store->accounts[0].server[0] == '\0')
                bstd_sprintf(store->accounts[0].server, sizeof(store->accounts[0].server), "%s", legacy.server);
        }
    }

    if (changed == TRUE || legacy_json == TRUE)
        i_save_account_store(store);
    return TRUE;
}

static bool_t i_apply_account(Backend *backend, const CalAccount *account)
{
    if (backend == NULL || account == NULL)
        return FALSE;

    bstd_sprintf(backend->account_name, sizeof(backend->account_name), "%s", account->name);
    bstd_sprintf(backend->account_marker, sizeof(backend->account_marker), "%s", account->name);
    bstd_sprintf(backend->jid, sizeof(backend->jid), "%s", account->jid);
    bstd_sprintf(backend->password, sizeof(backend->password), "%s", account->password);
    bstd_sprintf(backend->server, sizeof(backend->server), "%s", account->server);
    backend->auto_connect = account->auto_connect;
    return TRUE;
}

static bool_t i_apply_active_account(Backend *backend)
{
    AccountStore store;
    uint32_t index;

    if (backend == NULL || i_load_account_store(&store) == FALSE)
        return FALSE;
    if (store.count == 0)
    {
        bstd_sprintf(backend->account_name, sizeof(backend->account_name), "%s", "Me");
        backend->account_marker[0] = '\0';
        backend->jid[0] = '\0';
        backend->password[0] = '\0';
        backend->server[0] = '\0';
        backend->auto_connect = FALSE;
        return TRUE;
    }
    index = i_find_account(&store, store.active);
    if (index == CALANTAS_MAX_ACCOUNTS)
        index = 0;
    return i_apply_account(backend, &store.accounts[index]);
}

bool_t cal_backend_apply_active(Backend *backend)
{
    return i_apply_active_account(backend);
}

static bool_t i_save_active_account(const Backend *backend)
{
    AccountStore store;
    CalAccount account;
    uint32_t index;

    if (backend == NULL || i_load_account_store(&store) == FALSE)
        return FALSE;

    memset(&account, 0, sizeof(account));
    bstd_sprintf(account.name, sizeof(account.name), "%s",
                 backend->account_marker[0] != '\0' ? backend->account_marker : "XMPP");
    bstd_sprintf(account.jid, sizeof(account.jid), "%s", backend->jid);
    bstd_sprintf(account.password, sizeof(account.password), "%s", backend->password);
    bstd_sprintf(account.server, sizeof(account.server), "%s", backend->server);
    account.auto_connect = backend->auto_connect;

    index = i_find_account(&store, store.active);
    if (index == CALANTAS_MAX_ACCOUNTS)
        index = i_find_account(&store, account.name);
    if (index == CALANTAS_MAX_ACCOUNTS && store.count < CALANTAS_MAX_ACCOUNTS)
    {
        store.accounts[store.count] = account;
        index = store.count;
        store.count += 1;
    }
    else if (index == CALANTAS_MAX_ACCOUNTS)
    {
        return FALSE;
    }
    else
    {
        store.accounts[index] = account;
    }

    bstd_sprintf(store.active, sizeof(store.active), "%s", account.name);
    return i_save_account_store(&store);
}

static cal_status_t i_parse_status(const char_t *text)
{
    unsigned long value = strtoul(text, NULL, 10);

    switch (value)
    {
    case 1:
        return ekCAL_AWAY;
    case 2:
        return ekCAL_BUSY;
    case 3:
        return ekCAL_OFFLINE;
    case 0:
    default:
        return ekCAL_AVAILABLE;
    }
}

static uint32_t i_parse_u32(const char_t *text, const uint32_t max)
{
    unsigned long value = strtoul(text, NULL, 10);
    return value > (unsigned long)max ? max : (uint32_t)value;
}

static void i_write_string(FILE *file, const char_t *prefix, const char_t *text)
{
    fputs(prefix, file);
    i_write_escaped(file, text);
    fputs("|\n", file);
}

Backend *cal_backend_create(void)
{
    Backend *backend = heap_new0(Backend);

    backend->xmpp = cal_xmpp_create();
    backend->status = ekCAL_AVAILABLE;
    bstd_sprintf(backend->status_text, sizeof(backend->status_text), "%s", "Available with Calantas");
    bstd_sprintf(backend->account_name, sizeof(backend->account_name), "%s", "Me");
    bstd_sprintf(backend->connection_text, sizeof(backend->connection_text), "%s", "XMPP not connected");
    i_apply_active_account(backend);
    return backend;
}

void cal_backend_destroy(Backend **backend)
{
    if (backend == NULL || *backend == NULL)
        return;

    cal_xmpp_destroy(&(*backend)->xmpp);
    heap_delete(backend, Backend);
}

bool_t cal_backend_xmpp_available(void)
{
    return cal_xmpp_available();
}

bool_t cal_backend_connect(Backend *backend, const char_t *jid, const char_t *password, const char_t *server)
{
    if (backend == NULL || jid == NULL || jid[0] == '\0')
        return FALSE;

    bstd_sprintf(backend->jid, sizeof(backend->jid), "%s", jid);
    bstd_sprintf(backend->server, sizeof(backend->server), "%s",
                 server != NULL ? server : "");
    bstd_sprintf(backend->password, sizeof(backend->password), "%s",
                 password != NULL ? password : "");

    i_save_active_account(backend);
    cal_backend_store(backend);

    if (cal_xmpp_connect(backend->xmpp, jid, password, server) == FALSE)
    {
        bstd_sprintf(backend->connection_text, sizeof(backend->connection_text), "%s",
                     "XMPP connection could not start");
        return FALSE;
    }
    bstd_sprintf(backend->connection_text, sizeof(backend->connection_text),
                 "Connecting to %s", jid);

    return TRUE;
}

bool_t cal_backend_test_connection(const char_t *jid, const char_t *password, const char_t *server)
{
    if (cal_backend_xmpp_available() == FALSE)
        return FALSE;
    return cal_xmpp_test_connection(jid, password, server);
}

bool_t cal_backend_is_connected(const Backend *backend)
{
    if (backend == NULL)
        return FALSE;
    return cal_xmpp_is_connected(backend->xmpp);
}

const char_t *cal_backend_connection_text(const Backend *backend)
{
    return backend != NULL ? backend->connection_text : "";
}

uint32_t cal_backend_buddy_count(const Backend *backend)
{
    return backend != NULL ? backend->count : 0;
}

const CalBuddy *cal_backend_buddy(const Backend *backend, const uint32_t index)
{
    if (backend == NULL || index >= backend->count)
        return NULL;
    return &backend->buddies[index];
}

bool_t cal_backend_add_buddy(Backend *backend, const char_t *name, const char_t *handle)
{
    if (backend == NULL || name == NULL || name[0] == '\0')
        return FALSE;
    if (handle == NULL || handle[0] == '\0')
        return FALSE;
    if (backend->count >= CAL_MAX_BUDDIES)
        return FALSE;
    if (i_find_buddy(backend, handle) != CALANTAS_MAX_BUDDIES)
        return FALSE;

    cal_xmpp_add_buddy(backend->xmpp, handle, name);

    i_init_buddy(&backend->buddies[backend->count], name, handle, ekCAL_AVAILABLE, "XMPP buddy");
    backend->count += 1;
    return TRUE;
}

bool_t cal_backend_send(Backend *backend, const uint32_t index, const char_t *text)
{
    const CalBuddy *buddy;

    if (backend == NULL || index >= backend->count || text == NULL || text[0] == '\0')
        return FALSE;

    buddy = &backend->buddies[index];
    if (buddy->status == ekCAL_OFFLINE)
        return FALSE;

    if (cal_xmpp_send(backend->xmpp, buddy->handle, text) == FALSE)
        return FALSE;
    i_add_message(backend, index, TRUE, text);
    return TRUE;
}

uint32_t cal_backend_message_count(const Backend *backend, const uint32_t index)
{
    const CalBuddy *buddy = cal_backend_buddy(backend, index);
    return buddy != NULL ? buddy->message_count : 0;
}

const CalMessage *cal_backend_message(const Backend *backend, const uint32_t index,
                                      const uint32_t message)
{
    const CalBuddy *buddy = cal_backend_buddy(backend, index);

    if (buddy == NULL || message >= buddy->message_count)
        return NULL;

    return &buddy->messages[message];
}

bool_t cal_backend_set_status(Backend *backend, const cal_status_t status, const char_t *text)
{
    if (backend == NULL)
        return FALSE;

    backend->status = status;
    if (text == NULL || text[0] == '\0')
        text = "No status message";
    bstd_sprintf(backend->status_text, sizeof(backend->status_text), "%s", text);

    cal_xmpp_set_status(backend->xmpp, status, backend->status_text);
    return TRUE;
}

cal_status_t cal_backend_status(const Backend *backend)
{
    return backend != NULL ? backend->status : ekCAL_OFFLINE;
}

const char_t *cal_backend_status_text(const Backend *backend)
{
    return backend != NULL ? backend->status_text : "";
}

bool_t cal_backend_set_account_name(Backend *backend, const char_t *name)
{
    if (backend == NULL)
        return FALSE;

    if (name == NULL || name[0] == '\0')
        name = "Me";
    bstd_sprintf(backend->account_name, sizeof(backend->account_name), "%s", name);
    return TRUE;
}

const char_t *cal_backend_account_name(const Backend *backend)
{
    return backend != NULL ? backend->account_name : "";
}

const char_t *cal_backend_account_jid(const Backend *backend)
{
    return backend != NULL ? backend->jid : "";
}

const char_t *cal_backend_account_server(const Backend *backend)
{
    return backend != NULL ? backend->server : "";
}

const char_t *cal_backend_account_password(const Backend *backend)
{
    return backend != NULL ? backend->password : "";
}

bool_t cal_backend_set_auto_connect(Backend *backend, const bool_t auto_connect)
{
    if (backend == NULL)
        return FALSE;
    backend->auto_connect = auto_connect;
    return TRUE;
}

bool_t cal_backend_auto_connect(const Backend *backend)
{
    return backend != NULL ? backend->auto_connect : FALSE;
}

uint32_t cal_backend_accounts(CalAccount *out, const uint32_t max)
{
    AccountStore store;
    uint32_t count;
    uint32_t i;

    if (i_load_account_store(&store) == FALSE)
        return 0;
    count = store.count < max ? store.count : max;
    if (out != NULL)
    {
        for (i = 0; i < count; i += 1)
            out[i] = store.accounts[i];
    }
    return count;
}

bool_t cal_backend_account_get(const char_t *name, CalAccount *out)
{
    AccountStore store;
    uint32_t index;

    if (name == NULL || out == NULL || i_load_account_store(&store) == FALSE)
        return FALSE;
    index = i_find_account(&store, name);
    if (index == CALANTAS_MAX_ACCOUNTS)
        return FALSE;
    *out = store.accounts[index];
    return TRUE;
}

bool_t cal_backend_account_save(const CalAccount *account, const bool_t activate)
{
    AccountStore store;
    CalAccount copy;
    uint32_t index;

    if (account == NULL || i_load_account_store(&store) == FALSE)
        return FALSE;

    copy = *account;
    if (copy.name[0] == '\0')
        bstd_sprintf(copy.name, sizeof(copy.name), "%s", "XMPP");

    if (activate == TRUE && store.active[0] != '\0'
        && strcmp(store.active, copy.name) != 0)
    {
        index = i_find_account(&store, copy.name);
    }
    else if (activate == TRUE)
    {
        index = i_find_account(&store, store.active);
        if (index == CALANTAS_MAX_ACCOUNTS)
            index = i_find_account(&store, copy.name);
    }
    else
    {
        index = i_find_account(&store, copy.name);
    }

    if (index == CALANTAS_MAX_ACCOUNTS && store.count < CALANTAS_MAX_ACCOUNTS)
    {
        store.accounts[store.count] = copy;
        index = store.count;
        store.count += 1;
    }
    else if (index == CALANTAS_MAX_ACCOUNTS)
    {
        return FALSE;
    }
    else
    {
        store.accounts[index] = copy;
    }

    if (activate == TRUE)
        bstd_sprintf(store.active, sizeof(store.active), "%s", copy.name);
    return i_save_account_store(&store);
}

bool_t cal_backend_account_replace(const char_t *old_name, const CalAccount *account,
                                   const bool_t activate)
{
    AccountStore store;
    CalAccount copy;
    uint32_t index;

    if (old_name == NULL || account == NULL || i_load_account_store(&store) == FALSE)
        return FALSE;

    copy = *account;
    if (copy.name[0] == '\0')
        bstd_sprintf(copy.name, sizeof(copy.name), "%s", "XMPP");

    index = i_find_account(&store, old_name);
    if (index == CALANTAS_MAX_ACCOUNTS && store.count < CALANTAS_MAX_ACCOUNTS)
    {
        store.accounts[store.count] = copy;
        index = store.count;
        store.count += 1;
    }
    else if (index == CALANTAS_MAX_ACCOUNTS)
    {
        return FALSE;
    }
    else
    {
        store.accounts[index] = copy;
    }

    if (activate == TRUE)
        bstd_sprintf(store.active, sizeof(store.active), "%s", copy.name);
    return i_save_account_store(&store);
}

bool_t cal_backend_account_delete(const char_t *name)
{
    AccountStore store;
    uint32_t index;
    uint32_t i;

    if (name == NULL || i_load_account_store(&store) == FALSE)
        return FALSE;
    index = i_find_account(&store, name);
    if (index == CALANTAS_MAX_ACCOUNTS)
        return FALSE;

    for (i = index; i + 1 < store.count; i += 1)
        store.accounts[i] = store.accounts[i + 1];
    store.count -= 1;
    memset(&store.accounts[store.count], 0, sizeof(store.accounts[store.count]));

    if (store.count == 0)
        store.active[0] = '\0';
    else if (i_find_account(&store, store.active) == CALANTAS_MAX_ACCOUNTS)
        bstd_sprintf(store.active, sizeof(store.active), "%s", store.accounts[0].name);
    return i_save_account_store(&store);
}

const char_t *cal_backend_active_account(const Backend *backend)
{
    return backend != NULL ? backend->account_marker : "";
}

bool_t cal_backend_select_account(Backend *backend, const char_t *name)
{
    AccountStore store;
    uint32_t index;

    if (backend == NULL || name == NULL || i_load_account_store(&store) == FALSE)
        return FALSE;
    index = i_find_account(&store, name);
    if (index == CALANTAS_MAX_ACCOUNTS)
        return FALSE;

    bstd_sprintf(store.active, sizeof(store.active), "%s", store.accounts[index].name);
    if (i_save_account_store(&store) == FALSE)
        return FALSE;

    if (i_apply_account(backend, &store.accounts[index]) == FALSE)
        return FALSE;
    backend->count = 0;
    cal_backend_restore(backend);
    return TRUE;
}

bool_t cal_backend_poll(Backend *backend, CalEvent *event)
{
    if (backend == NULL || event == NULL)
        return FALSE;

    i_poll_xmpp_events(backend);

    if (backend->event_head == backend->event_tail)
        return FALSE;

    *event = backend->events[backend->event_head];
    backend->event_head = (backend->event_head + 1) % CAL_MAX_PENDING;
    return TRUE;
}

bool_t cal_backend_store(const Backend *backend)
{
    char_t path[1200];
    FILE *file;
    uint32_t i;
    uint32_t j;

    if (backend == NULL || i_state_path(path, (uint32_t)sizeof(path), backend->account_marker) == FALSE)
        return FALSE;

    file = fopen(path, "wb");
    if (file == NULL)
        return FALSE;

    i_write_string(file, "A|", backend->account_name);
    fprintf(file, "S|%u|", (unsigned)backend->status);
    i_write_escaped(file, backend->status_text);
    fputs("|\n", file);
    fprintf(file, "N|%u|\n", (unsigned)backend->count);

    for (i = 0; i < backend->count; i += 1)
    {
        const CalBuddy *buddy = &backend->buddies[i];

        fputs("B|", file);
        i_write_escaped(file, buddy->name);
        fputc('|', file);
        i_write_escaped(file, buddy->handle);
        fputc('|', file);
        fprintf(file, "%u|", (unsigned)buddy->status);
        i_write_escaped(file, buddy->status_text);
        fprintf(file, "|%u|\n", (unsigned)buddy->message_count);

        for (j = 0; j < buddy->message_count; j += 1)
        {
            const CalMessage *message = &buddy->messages[j];

            fputs("M|", file);
            i_write_escaped(file, message->time);
            fputc('|', file);
            i_write_escaped(file, message->sender);
            fprintf(file, "|%u|", message->own ? 1u : 0u);
            i_write_escaped(file, message->text);
            fputs("|\n", file);
        }
    }

    fclose(file);
#if !defined(_WIN32)
    chmod(path, 0600);
#endif
    return TRUE;
}

bool_t cal_backend_restore(Backend *backend)
{
    char_t path[1200];
    char_t line[CAL_LINE_MAX];
    char_t fields[8][CAL_FIELD_MAX];
    FILE *file;

    if (backend == NULL || i_state_path(path, (uint32_t)sizeof(path), backend->account_marker) == FALSE)
        return FALSE;

    file = fopen(path, "rb");
    if (file == NULL)
        return FALSE;

    while (fgets(line, sizeof(line), file) != NULL)
    {
        uint32_t nfields;

        i_strip_eol(line);
        nfields = i_split_fields(line, fields, 8);
        if (nfields < 2)
            continue;

        if (strcmp(fields[0], "A") == 0 && nfields >= 3)
        {
            bstd_sprintf(backend->account_name, sizeof(backend->account_name), "%s", fields[1]);
        }
        else if (strcmp(fields[0], "S") == 0 && nfields >= 4)
        {
            backend->status = i_parse_status(fields[1]);
            bstd_sprintf(backend->status_text, sizeof(backend->status_text), "%s", fields[2]);
        }
        else if (strcmp(fields[0], "N") == 0 && nfields >= 3)
        {
            backend->count = i_parse_u32(fields[1], CAL_MAX_BUDDIES);
        }
        else if (strcmp(fields[0], "B") == 0 && nfields >= 7)
        {
            CalBuddy *buddy;

            if (backend->count >= CAL_MAX_BUDDIES)
                continue;
            buddy = &backend->buddies[backend->count];
            i_init_buddy(buddy, fields[1], fields[2], i_parse_status(fields[3]), fields[4]);
            buddy->message_count = i_parse_u32(fields[5], CALANTAS_MAX_MESSAGES);
            backend->count += 1;
        }
        else if (strcmp(fields[0], "M") == 0 && nfields >= 6)
        {
            CalBuddy *buddy;
            CalMessage *message;

            if (backend->count == 0)
                continue;
            buddy = &backend->buddies[backend->count - 1];
            if (buddy->message_count >= CALANTAS_MAX_MESSAGES)
                continue;
            message = &buddy->messages[buddy->message_count];
            bstd_sprintf(message->time, sizeof(message->time), "%s", fields[1]);
            bstd_sprintf(message->sender, sizeof(message->sender), "%s", fields[2]);
            message->own = strcmp(fields[3], "1") == 0 ? TRUE : FALSE;
            bstd_sprintf(message->text, sizeof(message->text), "%s", fields[4]);
            buddy->message_count += 1;
        }
    }

    fclose(file);
    i_apply_active_account(backend);
    if (backend->jid[0] != '\0')
        bstd_sprintf(backend->connection_text, sizeof(backend->connection_text),
                     "XMPP not connected");
    return TRUE;
}
