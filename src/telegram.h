#ifndef CALANTAS_TELEGRAM_H
#define CALANTAS_TELEGRAM_H

#include "backend.h"

#ifndef CALANTAS_HAVE_TELEGRAM
#define CALANTAS_HAVE_TELEGRAM 0
#endif

#define CAL_TG_MAX_EVENTS 64
#define CAL_TG_MAX_OUT 64

typedef struct _telegram_backend_t TelegramBackend;

typedef enum _cal_tg_event_type_t
{
    ekCAL_TG_EVENT_CONTACT = 0,
    ekCAL_TG_EVENT_MESSAGE,
    ekCAL_TG_EVENT_CONNECTION
} cal_tg_event_type_t;

typedef enum _cal_tg_out_type_t
{
    ekCAL_TG_OUT_MESSAGE = 0,
    ekCAL_TG_OUT_CHAT
} cal_tg_out_type_t;

typedef struct _cal_tg_event_t
{
    cal_tg_event_type_t type;
    char_t name[64];
    char_t jid[128];
    char_t text[256];
} CalTelegramEvent;

typedef struct _cal_tg_out_t
{
    cal_tg_out_type_t type;
    char_t name[64];
    char_t jid[128];
    char_t text[256];
} CalTelegramOut;

TelegramBackend *cal_telegram_create(void);
void cal_telegram_destroy(TelegramBackend **backend);
bool_t cal_telegram_available(void);
bool_t cal_telegram_connect(TelegramBackend *backend, const char_t *identity,
                            const char_t *password, const char_t *api_id,
                            const char_t *api_hash);
bool_t cal_telegram_test_connection(const char_t *identity, const char_t *password,
                                    const char_t *api_id, const char_t *api_hash);
bool_t cal_telegram_is_connected(const TelegramBackend *backend);
bool_t cal_telegram_pop_event(TelegramBackend *backend, CalTelegramEvent *event);
bool_t cal_telegram_send(TelegramBackend *backend, const char_t *chat_id, const char_t *text);
bool_t cal_telegram_set_status(TelegramBackend *backend, const cal_status_t status,
                               const char_t *text);
bool_t cal_telegram_add_buddy(TelegramBackend *backend, const char_t *chat_id,
                              const char_t *name);
bool_t cal_telegram_disconnect(TelegramBackend *backend);

#endif
