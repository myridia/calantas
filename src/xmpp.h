#ifndef CALANTAS_XMPP_H
#define CALANTAS_XMPP_H

#include "backend.h"

#ifndef CALANTAS_HAVE_XMPP
#define CALANTAS_HAVE_XMPP 0
#endif

#define CAL_XMPP_MAX_EVENTS 64
#define CAL_XMPP_MAX_OUT 64

typedef struct _xmpp_backend_t XmppBackend;

typedef enum _cal_xmpp_event_type_t
{
    ekCAL_XMPP_EVENT_CONTACT = 0,
    ekCAL_XMPP_EVENT_PRESENCE,
    ekCAL_XMPP_EVENT_MESSAGE,
    ekCAL_XMPP_EVENT_CONNECTION
} cal_xmpp_event_type_t;

typedef enum _cal_xmpp_out_type_t
{
    ekCAL_XMPP_OUT_MESSAGE = 0,
    ekCAL_XMPP_OUT_PRESENCE,
    ekCAL_XMPP_OUT_ADD
} cal_xmpp_out_type_t;

typedef struct _cal_xmpp_event_t
{
    cal_xmpp_event_type_t type;
    char_t name[64];
    char_t jid[128];
    char_t text[256];
    cal_status_t status;
} CalXmppEvent;

typedef struct _cal_xmpp_out_t
{
    cal_xmpp_out_type_t type;
    char_t name[64];
    char_t jid[128];
    char_t text[256];
    cal_status_t status;
} CalXmppOut;

XmppBackend *cal_xmpp_create(void);
void cal_xmpp_destroy(XmppBackend **backend);
bool_t cal_xmpp_available(void);
bool_t cal_xmpp_connect(XmppBackend *backend, const char_t *jid, const char_t *password, const char_t *server);
bool_t cal_xmpp_test_connection(const char_t *jid, const char_t *password, const char_t *server);
bool_t cal_xmpp_is_connected(const XmppBackend *backend);
bool_t cal_xmpp_pop_event(XmppBackend *backend, CalXmppEvent *event);
bool_t cal_xmpp_send(XmppBackend *backend, const char_t *jid, const char_t *text);
bool_t cal_xmpp_set_status(XmppBackend *backend, const cal_status_t status, const char_t *text);
bool_t cal_xmpp_add_buddy(XmppBackend *backend, const char_t *jid, const char_t *name);

#endif
