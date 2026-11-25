#include "xmpp.h"

#include <nappgui.h>
#include <core/core.h>
#include <osbs/osbsall.h>
#include <sewer/bstd.h>

#include <string.h>

#if CALANTAS_HAVE_XMPP
#include <strophe.h>
#endif

struct _xmpp_backend_t
{
    Mutex *mutex;
    CalXmppEvent events[CAL_XMPP_MAX_EVENTS];
    uint32_t event_head;
    uint32_t event_tail;
    CalXmppOut outgoing[CAL_XMPP_MAX_OUT];
    uint32_t out_head;
    uint32_t out_tail;
    char_t jid[128];
    char_t password[128];
    char_t server[128];
    char_t status_text[128];
    cal_status_t status;
#if CALANTAS_HAVE_XMPP
    Thread *thread;
    xmpp_ctx_t *ctx;
    xmpp_conn_t *conn;
    bool_t running;
    bool_t connected;
#endif
};

static bool_t i_push_event(XmppBackend *backend, const CalXmppEvent *event)
{
    bool_t pushed = FALSE;

    bmutex_lock(backend->mutex);
    if ((backend->event_tail + 1) % CAL_XMPP_MAX_EVENTS != backend->event_head)
    {
        backend->events[backend->event_tail] = *event;
        backend->event_tail = (backend->event_tail + 1) % CAL_XMPP_MAX_EVENTS;
        pushed = TRUE;
    }
    bmutex_unlock(backend->mutex);

    return pushed;
}

bool_t cal_xmpp_pop_event(XmppBackend *backend, CalXmppEvent *event)
{
    bool_t popped = FALSE;

    if (backend == NULL || event == NULL)
        return FALSE;

    bmutex_lock(backend->mutex);
    if (backend->event_head != backend->event_tail)
    {
        *event = backend->events[backend->event_head];
        backend->event_head = (backend->event_head + 1) % CAL_XMPP_MAX_EVENTS;
        popped = TRUE;
    }
    bmutex_unlock(backend->mutex);

    return popped;
}

static bool_t i_push_out(XmppBackend *backend, const CalXmppOut *item)
{
    bool_t pushed = FALSE;

    bmutex_lock(backend->mutex);
    if ((backend->out_tail + 1) % CAL_XMPP_MAX_OUT != backend->out_head)
    {
        backend->outgoing[backend->out_tail] = *item;
        backend->out_tail = (backend->out_tail + 1) % CAL_XMPP_MAX_OUT;
        pushed = TRUE;
    }
    bmutex_unlock(backend->mutex);

    return pushed;
}

static bool_t i_pop_out(XmppBackend *backend, CalXmppOut *item)
{
    bool_t popped = FALSE;

    bmutex_lock(backend->mutex);
    if (backend->out_head != backend->out_tail)
    {
        *item = backend->outgoing[backend->out_head];
        backend->out_head = (backend->out_head + 1) % CAL_XMPP_MAX_OUT;
        popped = TRUE;
    }
    bmutex_unlock(backend->mutex);

    return popped;
}

static void i_connection_event(XmppBackend *backend, const char_t *text)
{
    CalXmppEvent event;

    memset(&event, 0, sizeof(event));
    event.type = ekCAL_XMPP_EVENT_CONNECTION;
    bstd_sprintf(event.text, sizeof(event.text), "%s", text != NULL ? text : "XMPP connection changed");
    i_push_event(backend, &event);
}

XmppBackend *cal_xmpp_create(void)
{
    XmppBackend *backend = heap_new0(XmppBackend);

    backend->mutex = bmutex_create();
    backend->status = ekCAL_AVAILABLE;
    bstd_sprintf(backend->status_text, sizeof(backend->status_text), "%s", "Available");
    return backend;
}

#if CALANTAS_HAVE_XMPP

static void i_stop_thread(XmppBackend *backend)
{
    if (backend->thread == NULL)
        return;

    bmutex_lock(backend->mutex);
    backend->running = FALSE;
    bmutex_unlock(backend->mutex);

    bthread_wait(backend->thread);
    bthread_close(&backend->thread);
}

static xmpp_stanza_t *i_text_child(xmpp_ctx_t *ctx, xmpp_stanza_t *parent,
                                   const char_t *name, const char_t *text)
{
    xmpp_stanza_t *child;

    if (text == NULL || text[0] == '\0')
        return NULL;

    child = xmpp_stanza_new(ctx);
    if (child == NULL)
        return NULL;
    xmpp_stanza_set_name(child, name);
    xmpp_stanza_set_text(child, text);
    xmpp_stanza_add_child(parent, child);
    return child;
}

static void i_send_presence(XmppBackend *backend, const cal_status_t status, const char_t *text)
{
    xmpp_ctx_t *ctx;
    xmpp_conn_t *conn;
    xmpp_stanza_t *presence;

    bmutex_lock(backend->mutex);
    ctx = backend->ctx;
    conn = backend->conn;
    bmutex_unlock(backend->mutex);

    if (ctx == NULL || conn == NULL)
        return;

    presence = xmpp_presence_new(ctx);
    if (presence == NULL)
        return;

    if (status == ekCAL_OFFLINE)
        xmpp_stanza_set_attribute(presence, "type", "unavailable");
    else if (status == ekCAL_AWAY)
        i_text_child(ctx, presence, "show", "away");
    else if (status == ekCAL_BUSY)
        i_text_child(ctx, presence, "show", "dnd");

    i_text_child(ctx, presence, "status", text);
    xmpp_send(conn, presence);
    xmpp_stanza_release(presence);
}

static void i_request_roster(XmppBackend *backend)
{
    xmpp_ctx_t *ctx;
    xmpp_conn_t *conn;
    xmpp_stanza_t *iq;
    xmpp_stanza_t *query;

    bmutex_lock(backend->mutex);
    ctx = backend->ctx;
    conn = backend->conn;
    bmutex_unlock(backend->mutex);

    if (ctx == NULL || conn == NULL)
        return;

    iq = xmpp_iq_new(ctx, "get", NULL);
    if (iq == NULL)
        return;

    query = xmpp_stanza_new(ctx);
    if (query != NULL)
    {
        xmpp_stanza_set_name(query, "query");
        xmpp_stanza_set_attribute(query, "xmlns", XMPP_NS_ROSTER);
        xmpp_stanza_add_child(iq, query);
    }

    xmpp_send(conn, iq);
    xmpp_stanza_release(iq);
}

static void i_send_roster_add(XmppBackend *backend, const char_t *jid, const char_t *name)
{
    xmpp_ctx_t *ctx;
    xmpp_conn_t *conn;
    xmpp_stanza_t *iq;
    xmpp_stanza_t *query;
    xmpp_stanza_t *item;
    xmpp_stanza_t *subscribe;

    bmutex_lock(backend->mutex);
    ctx = backend->ctx;
    conn = backend->conn;
    bmutex_unlock(backend->mutex);

    if (ctx == NULL || conn == NULL || jid == NULL || jid[0] == '\0')
        return;

    iq = xmpp_iq_new(ctx, "set", NULL);
    if (iq != NULL)
    {
        query = xmpp_stanza_new(ctx);
        if (query != NULL)
        {
            xmpp_stanza_set_name(query, "query");
            xmpp_stanza_set_attribute(query, "xmlns", XMPP_NS_ROSTER);
            item = xmpp_stanza_new(ctx);
            if (item != NULL)
            {
                xmpp_stanza_set_name(item, "item");
                xmpp_stanza_set_attribute(item, "jid", jid);
                if (name != NULL && name[0] != '\0')
                    xmpp_stanza_set_attribute(item, "name", name);
                xmpp_stanza_add_child(query, item);
            }
            xmpp_stanza_add_child(iq, query);
        }
        xmpp_send(conn, iq);
        xmpp_stanza_release(iq);
    }

    subscribe = xmpp_presence_new(ctx);
    if (subscribe != NULL)
    {
        xmpp_stanza_set_attribute(subscribe, "to", jid);
        xmpp_stanza_set_attribute(subscribe, "type", "subscribe");
        xmpp_send(conn, subscribe);
        xmpp_stanza_release(subscribe);
    }
}

static int i_stop_handler(xmpp_ctx_t *ctx, void *userdata)
{
    XmppBackend *backend = (XmppBackend *)userdata;
    bool_t stop;

    bmutex_lock(backend->mutex);
    stop = backend->running == FALSE;
    bmutex_unlock(backend->mutex);

    if (stop == TRUE)
    {
        xmpp_stop(ctx);
        return 0;
    }
    return 1;
}

static int i_out_handler(xmpp_ctx_t *ctx, void *userdata)
{
    XmppBackend *backend = (XmppBackend *)userdata;
    CalXmppOut item;
    bool_t connected;

    while (i_pop_out(backend, &item) == TRUE)
    {
        bmutex_lock(backend->mutex);
        connected = backend->connected;
        bmutex_unlock(backend->mutex);

        if (connected == FALSE)
            continue;

        if (item.type == ekCAL_XMPP_OUT_MESSAGE)
        {
            xmpp_stanza_t *message = xmpp_message_new(ctx, "chat", item.jid, NULL);
            if (message != NULL)
            {
                xmpp_message_set_body(message, item.text);
                xmpp_send(backend->conn, message);
                xmpp_stanza_release(message);
            }
        }
        else if (item.type == ekCAL_XMPP_OUT_PRESENCE)
        {
            i_send_presence(backend, item.status, item.text);
        }
        else if (item.type == ekCAL_XMPP_OUT_ADD)
        {
            i_send_roster_add(backend, item.jid, item.name);
        }
    }

    return 1;
}

static void i_conn_handler(xmpp_conn_t *conn, xmpp_conn_event_t event,
                           int error, xmpp_stream_error_t *stream_error,
                           void *userdata)
{
    XmppBackend *backend = (XmppBackend *)userdata;

    unref(conn);
    unref(error);
    unref(stream_error);

    if (event == XMPP_CONN_CONNECT)
    {
        char_t status_text[128];
        cal_status_t status;

        bmutex_lock(backend->mutex);
        backend->connected = TRUE;
        status = backend->status;
        bstd_sprintf(status_text, sizeof(status_text), "%s", backend->status_text);
        bmutex_unlock(backend->mutex);
        i_connection_event(backend, "XMPP connected");
        i_send_presence(backend, status, status_text);
        i_request_roster(backend);
    }
    else if (event == XMPP_CONN_FAIL)
    {
        bmutex_lock(backend->mutex);
        backend->connected = FALSE;
        bmutex_unlock(backend->mutex);
        i_connection_event(backend, "XMPP connection failed");
    }
    else
    {
        bmutex_lock(backend->mutex);
        backend->connected = FALSE;
        bmutex_unlock(backend->mutex);
        i_connection_event(backend, "XMPP disconnected");
    }
}

static int i_iq_handler(xmpp_conn_t *conn, xmpp_stanza_t *stanza, void *userdata)
{
    XmppBackend *backend = (XmppBackend *)userdata;
    const char_t *type = xmpp_stanza_get_type(stanza);
    xmpp_stanza_t *query;
    xmpp_stanza_t *item;
    const char_t *xmlns;

    unref(conn);

    if (type != NULL && strcmp(type, "error") == 0)
        return 1;

    query = xmpp_stanza_get_child_by_name(stanza, "query");
    if (query == NULL)
        return 1;

    xmlns = xmpp_stanza_get_attribute(query, "xmlns");
    if (xmlns == NULL || strcmp(xmlns, XMPP_NS_ROSTER) != 0)
        return 1;

    for (item = xmpp_stanza_get_children(query); item != NULL; item = xmpp_stanza_get_next(item))
    {
        const char_t *jid = xmpp_stanza_get_attribute(item, "jid");
        const char_t *name = xmpp_stanza_get_attribute(item, "name");
        const char_t *subscription = xmpp_stanza_get_attribute(item, "subscription");
        CalXmppEvent event;

        if (jid == NULL || jid[0] == '\0')
            continue;

        memset(&event, 0, sizeof(event));
        event.type = ekCAL_XMPP_EVENT_CONTACT;
        event.status = (subscription != NULL && strcmp(subscription, "remove") == 0)
                           ? ekCAL_OFFLINE : ekCAL_AVAILABLE;
        bstd_sprintf(event.name, sizeof(event.name), "%s",
                     name != NULL && name[0] != '\0' ? name : jid);
        bstd_sprintf(event.jid, sizeof(event.jid), "%s", jid);
        i_push_event(backend, &event);
    }

    return 1;
}

static int i_message_handler(xmpp_conn_t *conn, xmpp_stanza_t *stanza, void *userdata)
{
    XmppBackend *backend = (XmppBackend *)userdata;
    xmpp_ctx_t *ctx = xmpp_conn_get_context(conn);
    const char_t *from = xmpp_stanza_get_from(stanza);
    xmpp_stanza_t *body;
    const char_t *text;
    char_t *bare;
    CalXmppEvent event;

    if (from == NULL)
        return 1;

    body = xmpp_stanza_get_child_by_name(stanza, "body");
    if (body == NULL)
        return 1;

    text = xmpp_stanza_get_text_ptr(body);
    if (text == NULL || text[0] == '\0')
        return 1;

    bare = xmpp_jid_bare(ctx, from);
    memset(&event, 0, sizeof(event));
    event.type = ekCAL_XMPP_EVENT_MESSAGE;
    bstd_sprintf(event.jid, sizeof(event.jid), "%s", bare != NULL ? bare : from);
    bstd_sprintf(event.text, sizeof(event.text), "%s", text);
    i_push_event(backend, &event);
    if (bare != NULL)
        xmpp_free(ctx, bare);

    return 1;
}

static cal_status_t i_presence_status(xmpp_stanza_t *stanza)
{
    const char_t *type = xmpp_stanza_get_type(stanza);
    xmpp_stanza_t *show;
    const char_t *value;

    if (type != NULL && strcmp(type, "unavailable") == 0)
        return ekCAL_OFFLINE;

    show = xmpp_stanza_get_child_by_name(stanza, "show");
    if (show == NULL)
        return ekCAL_AVAILABLE;

    value = xmpp_stanza_get_text_ptr(show);
    if (value == NULL)
        return ekCAL_AVAILABLE;
    if (strcmp(value, "away") == 0 || strcmp(value, "xa") == 0)
        return ekCAL_AWAY;
    if (strcmp(value, "dnd") == 0)
        return ekCAL_BUSY;
    return ekCAL_AVAILABLE;
}

static int i_presence_handler(xmpp_conn_t *conn, xmpp_stanza_t *stanza, void *userdata)
{
    XmppBackend *backend = (XmppBackend *)userdata;
    xmpp_ctx_t *ctx = xmpp_conn_get_context(conn);
    const char_t *from = xmpp_stanza_get_from(stanza);
    xmpp_stanza_t *status_child;
    const char_t *status_text;
    char_t own_jid[128];
    char_t *own_bare;
    char_t *bare;
    CalXmppEvent event;

    if (from == NULL)
        return 1;

    bmutex_lock(backend->mutex);
    bstd_sprintf(own_jid, sizeof(own_jid), "%s", backend->jid);
    bmutex_unlock(backend->mutex);

    own_bare = xmpp_jid_bare(ctx, own_jid);
    bare = xmpp_jid_bare(ctx, from);
    if (own_bare != NULL && bare != NULL && strcmp(own_bare, bare) == 0)
    {
        if (own_bare != NULL)
            xmpp_free(ctx, own_bare);
        if (bare != NULL)
            xmpp_free(ctx, bare);
        return 1;
    }

    memset(&event, 0, sizeof(event));
    event.type = ekCAL_XMPP_EVENT_PRESENCE;
    event.status = i_presence_status(stanza);
    bstd_sprintf(event.jid, sizeof(event.jid), "%s", bare != NULL ? bare : from);

    status_child = xmpp_stanza_get_child_by_name(stanza, "status");
    status_text = status_child != NULL ? xmpp_stanza_get_text_ptr(status_child) : NULL;
    bstd_sprintf(event.text, sizeof(event.text), "%s", status_text != NULL ? status_text : "");
    i_push_event(backend, &event);

    if (own_bare != NULL)
        xmpp_free(ctx, own_bare);
    if (bare != NULL)
        xmpp_free(ctx, bare);

    return 1;
}

static uint32_t i_thread_main(void *data)
{
    XmppBackend *backend = (XmppBackend *)data;
    char_t jid[128];
    char_t password[128];
    char_t server[128];
    xmpp_ctx_t *ctx;
    xmpp_conn_t *conn;

    xmpp_initialize();

    ctx = xmpp_ctx_new(NULL, NULL);
    if (ctx == NULL)
    {
        xmpp_shutdown();
        return 1;
    }

    conn = xmpp_conn_new(ctx);
    if (conn == NULL)
    {
        xmpp_ctx_free(ctx);
        xmpp_shutdown();
        return 1;
    }

    bmutex_lock(backend->mutex);
    bstd_sprintf(jid, sizeof(jid), "%s", backend->jid);
    bstd_sprintf(password, sizeof(password), "%s", backend->password);
    bstd_sprintf(server, sizeof(server), "%s", backend->server);
    bmutex_unlock(backend->mutex);

    xmpp_conn_set_flags(conn, XMPP_CONN_FLAG_MANDATORY_TLS);
    xmpp_conn_set_jid(conn, jid);
    xmpp_conn_set_pass(conn, password);

    xmpp_handler_add(conn, i_message_handler, NULL, "message", NULL, backend);
    xmpp_handler_add(conn, i_presence_handler, NULL, "presence", NULL, backend);
    xmpp_handler_add(conn, i_iq_handler, NULL, "iq", NULL, backend);
    xmpp_global_timed_handler_add(ctx, i_out_handler, 500, backend);
    xmpp_global_timed_handler_add(ctx, i_stop_handler, 500, backend);

    bmutex_lock(backend->mutex);
    backend->ctx = ctx;
    backend->conn = conn;
    bmutex_unlock(backend->mutex);

    xmpp_connect_client(conn, server[0] != '\0' ? server : NULL, 0, i_conn_handler, backend);
    xmpp_run(ctx);

    bmutex_lock(backend->mutex);
    backend->ctx = NULL;
    backend->conn = NULL;
    backend->connected = FALSE;
    bmutex_unlock(backend->mutex);

    xmpp_conn_release(conn);
    xmpp_ctx_free(ctx);
    xmpp_shutdown();
    return 0;
}

#endif

void cal_xmpp_destroy(XmppBackend **backend)
{
    if (backend == NULL || *backend == NULL)
        return;

#if CALANTAS_HAVE_XMPP
    i_stop_thread(*backend);
#endif
    bmutex_close(&(*backend)->mutex);
    heap_delete(backend, XmppBackend);
}

bool_t cal_xmpp_available(void)
{
#if CALANTAS_HAVE_XMPP
    return TRUE;
#else
    return FALSE;
#endif
}

bool_t cal_xmpp_connect(XmppBackend *backend, const char_t *jid, const char_t *password, const char_t *server)
{
    if (backend == NULL || jid == NULL || jid[0] == '\0')
        return FALSE;

#if CALANTAS_HAVE_XMPP
    i_stop_thread(backend);

    bmutex_lock(backend->mutex);
    bstd_sprintf(backend->jid, sizeof(backend->jid), "%s", jid);
    bstd_sprintf(backend->password, sizeof(backend->password), "%s",
                 password != NULL ? password : "");
    bstd_sprintf(backend->server, sizeof(backend->server), "%s",
                 server != NULL ? server : "");
    backend->connected = FALSE;
    backend->running = TRUE;
    backend->event_head = 0;
    backend->event_tail = 0;
    backend->out_head = 0;
    backend->out_tail = 0;
    bmutex_unlock(backend->mutex);

    backend->thread = bthread_create(i_thread_main, backend, XmppBackend);
    if (backend->thread == NULL)
    {
        bmutex_lock(backend->mutex);
        backend->running = FALSE;
        bmutex_unlock(backend->mutex);
        i_connection_event(backend, "Could not start XMPP worker");
        return FALSE;
    }

    return TRUE;
#else
    i_connection_event(backend, "XMPP unavailable: build with libstrophe");
    return FALSE;
#endif
}

bool_t cal_xmpp_is_connected(const XmppBackend *backend)
{
#if CALANTAS_HAVE_XMPP
    bool_t connected;

    if (backend == NULL)
        return FALSE;

    bmutex_lock(backend->mutex);
    connected = backend->connected;
    bmutex_unlock(backend->mutex);
    return connected;
#else
    unref(backend);
    return FALSE;
#endif
}

bool_t cal_xmpp_send(XmppBackend *backend, const char_t *jid, const char_t *text)
{
    CalXmppOut item;

    if (backend == NULL || jid == NULL || jid[0] == '\0' || text == NULL || text[0] == '\0')
        return FALSE;
    if (cal_xmpp_available() == FALSE || cal_xmpp_is_connected(backend) == FALSE)
        return FALSE;

    memset(&item, 0, sizeof(item));
    item.type = ekCAL_XMPP_OUT_MESSAGE;
    bstd_sprintf(item.jid, sizeof(item.jid), "%s", jid);
    bstd_sprintf(item.text, sizeof(item.text), "%s", text);
    return i_push_out(backend, &item);
}

bool_t cal_xmpp_set_status(XmppBackend *backend, const cal_status_t status, const char_t *text)
{
    CalXmppOut item;

    if (backend == NULL || cal_xmpp_available() == FALSE)
        return FALSE;

    bmutex_lock(backend->mutex);
    backend->status = status;
    bstd_sprintf(backend->status_text, sizeof(backend->status_text), "%s",
                 text != NULL && text[0] != '\0' ? text : "No status message");
    bmutex_unlock(backend->mutex);

    memset(&item, 0, sizeof(item));
    item.type = ekCAL_XMPP_OUT_PRESENCE;
    item.status = status;
    bstd_sprintf(item.text, sizeof(item.text), "%s",
                 text != NULL && text[0] != '\0' ? text : "No status message");
    return i_push_out(backend, &item);
}

bool_t cal_xmpp_add_buddy(XmppBackend *backend, const char_t *jid, const char_t *name)
{
    CalXmppOut item;

    if (backend == NULL || jid == NULL || jid[0] == '\0')
        return FALSE;
    if (cal_xmpp_available() == FALSE || cal_xmpp_is_connected(backend) == FALSE)
        return FALSE;

    memset(&item, 0, sizeof(item));
    item.type = ekCAL_XMPP_OUT_ADD;
    bstd_sprintf(item.jid, sizeof(item.jid), "%s", jid);
    bstd_sprintf(item.name, sizeof(item.name), "%s", name != NULL ? name : "");
    return i_push_out(backend, &item);
}
