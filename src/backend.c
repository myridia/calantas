#include "backend.h"

#include <nappgui.h>
#include <sewer/bstd.h>

#include <stdlib.h>
#include <string.h>
#include <time.h>

#define CAL_MAX_BUDDIES CALANTAS_MAX_BUDDIES
#define CAL_MAX_PENDING 32

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
        bstd_sprintf(stamp, sizeof(stamp), "--:--");

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

Backend *cal_backend_create(void)
{
    Backend *backend = heap_new0(Backend);

    backend->status = ekCAL_AVAILABLE;
    bstd_sprintf(backend->status_text, sizeof(backend->status_text), "%s", "Available with Calantas");

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
