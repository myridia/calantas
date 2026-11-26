#ifndef CALANTAS_BACKEND_H
#define CALANTAS_BACKEND_H

#include <nappgui.h>

#define CALANTAS_MAX_TEXT 512
#define CALANTAS_MAX_MESSAGES 128
#define CALANTAS_MAX_BUDDIES 64
#define CALANTAS_MAX_ACCOUNTS 16

typedef enum _cal_status_t
{
    ekCAL_AVAILABLE = 0,
    ekCAL_AWAY,
    ekCAL_BUSY,
    ekCAL_OFFLINE
} cal_status_t;

typedef enum _cal_event_type_t
{
    ekCAL_EVENT_MESSAGE = 0,
    ekCAL_EVENT_CONTACTS,
    ekCAL_EVENT_CONNECTION
} cal_event_type_t;

typedef struct _cal_message_t
{
    char_t time[16];
    char_t sender[64];
    char_t text[CALANTAS_MAX_TEXT];
    bool_t own;
} CalMessage;

typedef struct _cal_buddy_t
{
    char_t name[64];
    char_t handle[128];
    cal_status_t status;
    char_t status_text[128];
    CalMessage messages[CALANTAS_MAX_MESSAGES];
    uint32_t message_count;
} CalBuddy;

typedef struct _cal_event_t
{
    cal_event_type_t type;
    uint32_t buddy;
    char_t text[256];
} CalEvent;

typedef struct _cal_account_t
{
    char_t name[64];
    char_t protocol[16];
    char_t jid[128];
    char_t password[128];
    char_t server[128];
    char_t api_id[32];
    char_t api_hash[64];
    bool_t auto_connect;
} CalAccount;

typedef struct _backend_t Backend;

typedef void (*CalTestLog)(const char_t *text, void *context);

Backend *cal_backend_create(void);
void cal_backend_destroy(Backend **backend);
bool_t cal_backend_xmpp_available(void);
bool_t cal_backend_telegram_available(void);
bool_t cal_backend_connect(Backend *backend, const char_t *protocol, const char_t *jid,
                           const char_t *password, const char_t *server,
                           const char_t *api_id, const char_t *api_hash);
bool_t cal_backend_test_connection(const char_t *protocol, const char_t *jid,
                                   const char_t *password, const char_t *server,
                                   const char_t *api_id, const char_t *api_hash);
bool_t cal_backend_test_connection_logged(const char_t *protocol, const char_t *jid,
                                          const char_t *password, const char_t *server,
                                          const char_t *api_id, const char_t *api_hash,
                                          CalTestLog log, void *context);
bool_t cal_backend_is_connected(const Backend *backend);
const char_t *cal_backend_connection_text(const Backend *backend);
uint32_t cal_backend_buddy_count(const Backend *backend);
const CalBuddy *cal_backend_buddy(const Backend *backend, uint32_t index);
bool_t cal_backend_add_buddy(Backend *backend, const char_t *name, const char_t *handle);
bool_t cal_backend_send(Backend *backend, uint32_t buddy, const char_t *text);
uint32_t cal_backend_message_count(const Backend *backend, uint32_t buddy);
const CalMessage *cal_backend_message(const Backend *backend, uint32_t buddy, uint32_t index);
bool_t cal_backend_set_status(Backend *backend, cal_status_t status, const char_t *text);
cal_status_t cal_backend_status(const Backend *backend);
const char_t *cal_backend_status_text(const Backend *backend);
bool_t cal_backend_set_account_name(Backend *backend, const char_t *name);
const char_t *cal_backend_account_name(const Backend *backend);
const char_t *cal_backend_account_jid(const Backend *backend);
const char_t *cal_backend_account_server(const Backend *backend);
const char_t *cal_backend_account_password(const Backend *backend);
const char_t *cal_backend_protocol(const Backend *backend);
const char_t *cal_backend_api_id(const Backend *backend);
const char_t *cal_backend_api_hash(const Backend *backend);
bool_t cal_backend_set_auto_connect(Backend *backend, const bool_t auto_connect);
bool_t cal_backend_auto_connect(const Backend *backend);
uint32_t cal_backend_accounts(CalAccount *out, const uint32_t max);
bool_t cal_backend_account_get(const char_t *name, CalAccount *out);
bool_t cal_backend_account_save(const CalAccount *account, const bool_t activate);
bool_t cal_backend_account_replace(const char_t *old_name, const CalAccount *account,
                                   const bool_t activate);
bool_t cal_backend_account_delete(const char_t *name);
const char_t *cal_backend_active_account(const Backend *backend);
bool_t cal_backend_apply_active(Backend *backend);
bool_t cal_backend_select_account(Backend *backend, const char_t *name);
bool_t cal_backend_store(const Backend *backend);
bool_t cal_backend_restore(Backend *backend);
bool_t cal_backend_poll(Backend *backend, CalEvent *event);

#endif
