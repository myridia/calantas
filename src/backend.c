#include "backend.h"

#include <nappgui.h>
#include <sewer/bstd.h>

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
#define CAL_MAX_PENDING 32
#define CAL_LINE_MAX 2048
#define CAL_FIELD_MAX CALANTAS_MAX_TEXT

typedef struct _pending_t
{
    bool_t active;
    uint32_t buddy;
    uint32_t due;
    char_t text[CALANTAS_MAX_TEXT];
} Pending;

struct _backend_t
{
    CalBuddy buddies[CAL_MAX_BUDDIES];
    uint32_t count;
    cal_status_t status;
    char_t status_text[128];
    char_t account_name[64];
    Pending pending[CAL_MAX_PENDING];
    CalEvent events[CAL_MAX_PENDING];
    uint32_t event_head;
    uint32_t event_tail;
    uint32_t tick;
    uint32_t reply;
};

static const char_t *i_replies[] = {
    "Got it. I'll take a look.",
    "That sounds good from here.",
    "Interesting - tell me more.",
    "Thanks, I received your message.",
    "Let me check and get back to you.",
    "Confirmed on this side."
};

static void i_init_buddy(CalBuddy *buddy, const char_t *name, const char_t *handle,
                         const cal_status_t status, const char_t *status_text)
{
    bstd_sprintf(buddy->name, sizeof(buddy->name), "%s", name);
    bstd_sprintf(buddy->handle, sizeof(buddy->handle), "%s", handle);
    buddy->status = status;
    bstd_sprintf(buddy->status_text, sizeof(buddy->status_text), "%s", status_text);
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

static void i_push_event(Backend *backend, const uint32_t buddy)
{
    if ((backend->event_tail + 1) % CAL_MAX_PENDING == backend->event_head)
        return;

    backend->events[backend->event_tail].buddy = buddy;
    backend->event_tail = (backend->event_tail + 1) % CAL_MAX_PENDING;
}

static bool_t i_config_path(char_t *out, const uint32_t size)
{
    char_t dir[1024];

#if defined(_WIN32)
    const char_t *base = getenv("APPDATA");
    if (base == NULL || base[0] == '\0')
        base = getenv("USERPROFILE");
    if (base == NULL || base[0] == '\0')
        return FALSE;
    bstd_sprintf(dir, sizeof(dir), "%s\\Calantas", base);
    CAL_CREATE_DIR(dir);
    bstd_sprintf(out, size, "%s\\state.txt", dir);
#else
    const char_t *home = getenv("HOME");
    char_t parent[1024];
    if (home == NULL || home[0] == '\0')
        return FALSE;
    bstd_sprintf(parent, sizeof(parent), "%s/.config", home);
    CAL_CREATE_DIR(parent);
    bstd_sprintf(dir, sizeof(dir), "%s/.config/calantas", home);
    CAL_CREATE_DIR(dir);
    bstd_sprintf(out, size, "%s/state.txt", dir);
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

    backend->status = ekCAL_AVAILABLE;
    bstd_sprintf(backend->status_text, sizeof(backend->status_text), "%s", "Available with Calantas");
    bstd_sprintf(backend->account_name, sizeof(backend->account_name), "%s", "Me");

    i_init_buddy(&backend->buddies[backend->count++], "Ada Lovelace", "ada@example.org",
                 ekCAL_AVAILABLE, "Working on engines");
    i_init_buddy(&backend->buddies[backend->count++], "Grace Hopper", "grace@example.org",
                 ekCAL_AVAILABLE, "Compiling something");
    i_init_buddy(&backend->buddies[backend->count++], "Alan Turing", "alan@example.org",
                 ekCAL_AWAY, "Thinking");
    i_init_buddy(&backend->buddies[backend->count++], "Katherine Johnson", "katherine@example.org",
                 ekCAL_AVAILABLE, "Checking the numbers");
    i_init_buddy(&backend->buddies[backend->count++], "Barbara Liskov", "barbara@example.org",
                 ekCAL_BUSY, "In a design review");
    i_init_buddy(&backend->buddies[backend->count++], "Edsger Dijkstra", "edsger@example.org",
                 ekCAL_OFFLINE, "Offline");

    return backend;
}

void cal_backend_destroy(Backend **backend)
{
    heap_delete(backend, Backend);
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

    i_init_buddy(&backend->buddies[backend->count], name, handle, ekCAL_AVAILABLE, "Local buddy");
    backend->count += 1;
    return TRUE;
}

bool_t cal_backend_send(Backend *backend, const uint32_t index, const char_t *text)
{
    uint32_t i;
    char_t reply[CALANTAS_MAX_TEXT];
    const CalBuddy *buddy;

    if (backend == NULL || index >= backend->count || text == NULL || text[0] == '\0')
        return FALSE;

    buddy = &backend->buddies[index];
    if (buddy->status == ekCAL_OFFLINE)
        return FALSE;

    i_add_message(backend, index, TRUE, text);
    bstd_sprintf(reply, sizeof(reply), "%s", i_replies[backend->reply % 6]);
    backend->reply += 1;

    for (i = 0; i < CAL_MAX_PENDING; i += 1)
    {
        if (backend->pending[i].active == FALSE)
        {
            backend->pending[i].active = TRUE;
            backend->pending[i].buddy = index;
            backend->pending[i].due = backend->tick + 2;
            bstd_sprintf(backend->pending[i].text, sizeof(backend->pending[i].text), "%s", reply);
            break;
        }
    }

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

bool_t cal_backend_poll(Backend *backend, CalEvent *event)
{
    uint32_t i;

    if (backend == NULL || event == NULL)
        return FALSE;

    backend->tick += 1;

    for (i = 0; i < CAL_MAX_PENDING; i += 1)
    {
        if (backend->pending[i].active == TRUE && backend->tick >= backend->pending[i].due)
        {
            i_add_message(backend, backend->pending[i].buddy, FALSE, backend->pending[i].text);
            i_push_event(backend, backend->pending[i].buddy);
            backend->pending[i].active = FALSE;
        }
    }

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

    if (backend == NULL || i_config_path(path, (uint32_t)sizeof(path)) == FALSE)
        return FALSE;

    file = fopen(path, "wb");
    if (file == NULL)
        return FALSE;

    fputs("CALANTAS1\n", file);
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

    if (backend == NULL || i_config_path(path, (uint32_t)sizeof(path)) == FALSE)
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
    return TRUE;
}
