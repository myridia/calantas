#ifndef CALANTAS_BACKEND_H
#define CALANTAS_BACKEND_H

#include <nappgui.h>

#define CALANTAS_MAX_TEXT 512
#define CALANTAS_MAX_MESSAGES 128
#define CALANTAS_MAX_BUDDIES 64

typedef enum _cal_status_t
{
    ekCAL_AVAILABLE = 0,
    ekCAL_AWAY,
    ekCAL_BUSY,
    ekCAL_OFFLINE
} cal_status_t;

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
    uint32_t buddy;
} CalEvent;

typedef struct _backend_t Backend;

Backend *cal_backend_create(void);
void cal_backend_destroy(Backend **backend);
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
bool_t cal_backend_store(const Backend *backend);
bool_t cal_backend_restore(Backend *backend);
bool_t cal_backend_poll(Backend *backend, CalEvent *event);

#endif
