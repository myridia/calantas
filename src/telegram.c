#include "telegram.h"

#include <nappgui.h>
#include <core/core.h>
#include <osbs/osbsall.h>
#include <sewer/bstd.h>

#include <stdlib.h>
#include <string.h>
#include <time.h>

#if CALANTAS_HAVE_TELEGRAM

struct TdJson;

extern struct TdJson *td_json_client_create(void);
extern void td_json_client_destroy(struct TdJson *client);
extern const char_t *td_json_client_receive(struct TdJson *client, const double timeout);
extern void td_json_client_send(struct TdJson *client, const char_t *request);

#endif

struct _telegram_backend_t
{
    Mutex *mutex;
    CalTelegramEvent events[CAL_TG_MAX_EVENTS];
    uint32_t event_head;
    uint32_t event_tail;
    CalTelegramOut outgoing[CAL_TG_MAX_OUT];
    uint32_t out_head;
    uint32_t out_tail;
    char_t identity[128];
    char_t password[128];
    char_t api_id[32];
    char_t api_hash[64];
    char_t status_text[128];
    cal_status_t status;
#if CALANTAS_HAVE_TELEGRAM
    char_t database[96];
    Thread *thread;
    struct TdJson *client;
    bool_t running;
    bool_t connected;
    bool_t ready_sent;
    bool_t parameters_sent;
#endif
};

static bool_t i_push_event(TelegramBackend *backend, const CalTelegramEvent *event)
{
    bool_t pushed = FALSE;

    bmutex_lock(backend->mutex);
    if ((backend->event_tail + 1) % CAL_TG_MAX_EVENTS != backend->event_head)
    {
        backend->events[backend->event_tail] = *event;
        backend->event_tail = (backend->event_tail + 1) % CAL_TG_MAX_EVENTS;
        pushed = TRUE;
    }
    bmutex_unlock(backend->mutex);
    return pushed;
}

bool_t cal_telegram_pop_event(TelegramBackend *backend, CalTelegramEvent *event)
{
    bool_t popped = FALSE;

    if (backend == NULL || event == NULL)
        return FALSE;

    bmutex_lock(backend->mutex);
    if (backend->event_head != backend->event_tail)
    {
        *event = backend->events[backend->event_head];
        backend->event_head = (backend->event_head + 1) % CAL_TG_MAX_EVENTS;
        popped = TRUE;
    }
    bmutex_unlock(backend->mutex);
    return popped;
}

static bool_t i_push_out(TelegramBackend *backend, const CalTelegramOut *item)
{
    bool_t pushed = FALSE;

    bmutex_lock(backend->mutex);
    if ((backend->out_tail + 1) % CAL_TG_MAX_OUT != backend->out_head)
    {
        backend->outgoing[backend->out_tail] = *item;
        backend->out_tail = (backend->out_tail + 1) % CAL_TG_MAX_OUT;
        pushed = TRUE;
    }
    bmutex_unlock(backend->mutex);
    return pushed;
}

static bool_t i_pop_out(TelegramBackend *backend, CalTelegramOut *item)
{
    bool_t popped = FALSE;

    bmutex_lock(backend->mutex);
    if (backend->out_head != backend->out_tail)
    {
        *item = backend->outgoing[backend->out_head];
        backend->out_head = (backend->out_head + 1) % CAL_TG_MAX_OUT;
        popped = TRUE;
    }
    bmutex_unlock(backend->mutex);
    return popped;
}

static void i_connection_event(TelegramBackend *backend, const char_t *text)
{
    CalTelegramEvent event;

    memset(&event, 0, sizeof(event));
    event.type = ekCAL_TG_EVENT_CONNECTION;
    bstd_sprintf(event.text, sizeof(event.text), "%s",
                 text != NULL ? text : "Telegram connection changed");
    i_push_event(backend, &event);
}

static const char_t *i_find_key(const char_t *json, const char_t *key)
{
    char_t quoted[96];
    const char_t *p;
    size_t n;

    if (json == NULL || key == NULL)
        return NULL;
    bstd_sprintf(quoted, sizeof(quoted), "\"%s\"", key);
    n = strlen(quoted);
    p = json;
    while ((p = strstr(p, quoted)) != NULL)
    {
        const char_t *value = p + n;

        while (*value == ' ' || *value == '\t' || *value == '\r' || *value == '\n')
            value += 1;
        if (*value == ':')
            return p;
        p += n;
    }
    return NULL;
}

static bool_t i_json_string(const char_t *json, const char_t *key, char_t *out,
                            const uint32_t size)
{
    char_t quoted[96];
    const char_t *search;
    size_t keylen;

    if (json == NULL || key == NULL || out == NULL || size == 0)
        return FALSE;
    bstd_sprintf(quoted, sizeof(quoted), "\"%s\"", key);
    keylen = strlen(quoted);
    search = json;

    for (;;)
    {
        const char_t *p = i_find_key(search, key);
        const char_t *value;
        uint32_t n = 0;

        if (p == NULL)
            return FALSE;
        value = p + keylen;
        while (*value == ' ' || *value == '\t' || *value == '\r' || *value == '\n')
            value += 1;
        if (*value == ':')
        {
            value += 1;
            while (*value == ' ' || *value == '\t' || *value == '\r' || *value == '\n')
                value += 1;
        }
        if (*value == '"')
        {
            value += 1;
            while (*value != '\0' && *value != '"' && n + 1 < size)
            {
                if (*value == '\\' && value[1] != '\0')
                {
                    value += 1;
                    if (*value == 'n')
                        out[n++] = '\n';
                    else if (*value == 'r')
                        out[n++] = '\r';
                    else if (*value == 't')
                        out[n++] = '\t';
                    else
                        out[n++] = *value;
                }
                else
                {
                    out[n++] = *value;
                }
                value += 1;
            }
            out[n] = '\0';
            return TRUE;
        }
        if ((*value >= '0' && *value <= '9') || *value == '-')
        {
            while (*value != '\0' && *value != ',' && *value != '}' && *value != ']'
                   && *value != ' ' && *value != '\t' && *value != '\r' && *value != '\n'
                   && n + 1 < size)
            {
                out[n++] = *value;
                value += 1;
            }
            out[n] = '\0';
            if (n > 0)
                return TRUE;
        }
        search = p + keylen;
    }
}

static bool_t i_json_bool(const char_t *json, const char_t *key)
{
    const char_t *p = i_find_key(json, key);
    char_t quoted[96];
    const char_t *value;

    if (p == NULL)
        return FALSE;
    bstd_sprintf(quoted, sizeof(quoted), "\"%s\"", key);
    value = p + strlen(quoted);
    while (*value == ' ' || *value == '\t' || *value == '\r' || *value == '\n')
        value += 1;
    if (*value == ':')
    {
        value += 1;
        while (*value == ' ' || *value == '\t' || *value == '\r' || *value == '\n')
            value += 1;
    }
    return strncmp(value, "true", 4) == 0 ? TRUE : FALSE;
}

static bool_t i_has(const char_t *json, const char_t *text)
{
    return json != NULL && text != NULL && strstr(json, text) != NULL ? TRUE : FALSE;
}

static void i_json_escape(char_t *dst, const uint32_t size, const char_t *src)
{
    uint32_t n = 0;
    const char_t *p;

    if (dst == NULL || size == 0)
        return;
    dst[0] = '\0';
    if (src == NULL)
        return;

    for (p = src; *p != '\0' && n + 7 < size; p += 1)
    {
        if (*p == '"')
        {
            dst[n++] = '\\';
            dst[n++] = '"';
        }
        else if (*p == '\\')
        {
            dst[n++] = '\\';
            dst[n++] = '\\';
        }
        else if (*p == '\n')
        {
            dst[n++] = '\\';
            dst[n++] = 'n';
        }
        else if (*p == '\r')
        {
            dst[n++] = '\\';
            dst[n++] = 'r';
        }
        else if (*p == '\t')
        {
            dst[n++] = '\\';
            dst[n++] = 't';
        }
        else
        {
            dst[n++] = *p;
        }
    }
    dst[n] = '\0';
}

static bool_t i_is_bot_token(const char_t *text)
{
    const char_t *p;

    if (text == NULL || text[0] == '\0')
        return FALSE;
    p = text;
    while (*p >= '0' && *p <= '9')
        p += 1;
    return p > text && *p == ':' && p[1] != '\0' ? TRUE : FALSE;
}

static uint32_t i_hash_text(const char_t *text, uint32_t hash)
{
    const unsigned char *p;

    if (text == NULL)
        return hash;
    for (p = (const unsigned char *)text; *p != '\0'; p += 1)
    {
        hash ^= *p;
        hash *= 16777619u;
    }
    return hash;
}

static void i_test_database(char_t *out, const uint32_t size, const char_t *identity,
                            const char_t *api_id, const char_t *api_hash)
{
    uint32_t hash = 2166136261u;

    hash = i_hash_text(identity, hash);
    hash = i_hash_text(api_id, hash);
    hash = i_hash_text(api_hash, hash);
    bstd_sprintf(out, size, "calantas-telegram-test-%08x", (unsigned int)hash);
}

static void i_secret_part(const char_t *secret, const bool_t first, char_t *out,
                          const uint32_t size)
{
    const char_t *source;
    size_t length;
    uint32_t n = 0;

    if (out == NULL || size == 0)
        return;
    out[0] = '\0';
    if (secret == NULL)
        return;

    source = secret;
    length = strlen(secret);
    if (strchr(secret, ':') != NULL)
    {
        const char_t *colon = strchr(secret, ':');

        if (first == TRUE)
            length = (size_t)(colon - secret);
        else
            source = colon + 1;
    }

    while (n + 1 < size && n < length && source[n] != '\0')
    {
        out[n] = source[n];
        n += 1;
    }
    out[n] = '\0';
}

static bool_t i_valid_api_id_text(const char_t *text)
{
    bool_t nonzero;
    size_t i;
    size_t length;

    if (text == NULL)
        return FALSE;
    length = strlen(text);
    if (length == 0 || length > 10)
        return FALSE;
    nonzero = FALSE;
    for (i = 0; i < length; i += 1)
    {
        if (text[i] < '0' || text[i] > '9')
            return FALSE;
        if (text[i] != '0')
            nonzero = TRUE;
    }
    return nonzero;
}

static int i_parse_api_id(const char_t *text)
{
    int value;
    const char_t *p;

    value = 0;
    if (text == NULL)
        return 0;
    for (p = text; *p >= '0' && *p <= '9'; p += 1)
    {
        value = value * 10 + (int)(*p - '0');
    }
    return value;
}

#if CALANTAS_HAVE_TELEGRAM

static void i_set_parameters(struct TdJson *client, const char_t *api_id,
                             const char_t *api_hash, const char_t *database_directory)
{
    char_t hash[256];
    char_t request[2048];

    i_json_escape(hash, sizeof(hash), api_hash);
    bstd_sprintf(request, sizeof(request),
                 "{\"@type\":\"setTdlibParameters\","
                 "\"use_test_dc\":false,"
                 "\"database_directory\":\"%s\","
                 "\"files_directory\":\"\","
                 "\"database_encryption_key\":\"\","
                 "\"use_file_database\":true,\"use_chat_info_database\":true,"
                 "\"use_message_database\":true,\"use_secret_chats\":false,"
                 "\"api_id\":%d,\"api_hash\":\"%s\","
                 "\"system_language_code\":\"en\",\"device_model\":\"Desktop\","
                 "\"system_version\":\"Calantas\",\"application_version\":\"1.0\","
                 "\"enable_storage_optimizer\":true,\"ignore_file_names\":false}",
                 database_directory, i_parse_api_id(api_id), hash);
    td_json_client_send(client, request);
}

static bool_t i_send_auth_request(struct TdJson *client, const char_t *state,
                                  const char_t *identity, const char_t *password,
                                  const char_t *api_id, const char_t *api_hash,
                                  const char_t *database_directory)
{
    char_t request[1024];
    char_t value[256];

    if (client == NULL || state == NULL)
        return FALSE;

    if (strcmp(state, "authorizationStateWaitTdlibParameters") == 0)
    {
        i_set_parameters(client, api_id, api_hash, database_directory);
        return TRUE;
    }
    if (strcmp(state, "authorizationStateWaitEncryptionKey") == 0)
    {
        td_json_client_send(client,
                            "{\"@type\":\"setDatabaseEncryptionKey\",\"new_encryption_key\":\"\"}");
        return TRUE;
    }
    if (strcmp(state, "authorizationStateWaitPhoneNumber") == 0
        || strcmp(state, "authorizationStateWaitBotToken") == 0)
    {
        i_json_escape(value, sizeof(value), identity);
        if (i_is_bot_token(identity) == TRUE)
            bstd_sprintf(request, sizeof(request),
                         "{\"@type\":\"setAuthenticationBotToken\",\"token\":\"%s\"}", value);
        else
            bstd_sprintf(request, sizeof(request),
                         "{\"@type\":\"setAuthenticationPhoneNumber\","
                         "\"phone_number\":\"%s\"}", value);
        td_json_client_send(client, request);
        return TRUE;
    }
    if (strcmp(state, "authorizationStateWaitCode") == 0)
    {
        char_t code[256];

        i_secret_part(password, TRUE, code, sizeof(code));
        if (code[0] == '\0')
            return FALSE;
        i_json_escape(value, sizeof(value), code);
        bstd_sprintf(request, sizeof(request),
                     "{\"@type\":\"checkAuthenticationCode\",\"code\":\"%s\"}", value);
        td_json_client_send(client, request);
        return TRUE;
    }
    if (strcmp(state, "authorizationStateWaitPassword") == 0)
    {
        char_t secret[256];

        i_secret_part(password, FALSE, secret, sizeof(secret));
        if (secret[0] == '\0')
            return FALSE;
        i_json_escape(value, sizeof(value), secret);
        bstd_sprintf(request, sizeof(request),
                     "{\"@type\":\"checkAuthenticationPassword\",\"password\":\"%s\"}", value);
        td_json_client_send(client, request);
        return TRUE;
    }
    if (strcmp(state, "authorizationStateWaitRegistration") == 0)
    {
        td_json_client_send(client,
                            "{\"@type\":\"registerUser\","
                            "\"first_name\":\"Calantas\",\"last_name\":\"User\"}");
        return TRUE;
    }
    return TRUE;
}

static bool_t i_auth_state(const char_t *json, char_t *state, const uint32_t size)
{
    const char_t *anchor;

    if (json == NULL || state == NULL || size == 0)
        return FALSE;
    state[0] = '\0';
    anchor = strstr(json, "\"authorization_state\"");
    if (anchor != NULL && i_json_string(anchor, "@type", state, size) == TRUE)
        return TRUE;
    if (i_json_string(json, "@type", state, size) == TRUE
        && strncmp(state, "authorizationState", 18) == 0)
        return TRUE;
    state[0] = '\0';
    return FALSE;
}

static void i_send_ready_requests(TelegramBackend *backend, struct TdJson *client)
{
    if (backend->ready_sent == TRUE)
        return;
    backend->ready_sent = TRUE;
    td_json_client_send(client, "{\"@type\":\"getContacts\"}");
    td_json_client_send(client,
                        "{\"@type\":\"getChats\",\"offset_chat_id\":\"0\",\"limit\":100}");
}

static void i_process_chat(TelegramBackend *backend, const char_t *json)
{
    const char_t *chat;
    char_t id[128];
    char_t title[64];
    char_t name[96];
    CalTelegramEvent event;

    chat = strstr(json, "\"chat\"");
    if (chat == NULL)
        chat = json;
    if (i_has(chat, "chatTypeSecret") == TRUE)
        return;
    if (i_json_string(chat, "id", id, sizeof(id)) == FALSE || id[0] == '\0')
        return;
    if (i_json_string(chat, "title", title, sizeof(title)) == FALSE || title[0] == '\0')
        bstd_sprintf(title, sizeof(title), "Telegram %s", id);

    memset(&event, 0, sizeof(event));
    event.type = ekCAL_TG_EVENT_CONTACT;
    bstd_sprintf(name, sizeof(name), "%s", title);
    bstd_sprintf(event.name, sizeof(event.name), "%s", name);
    bstd_sprintf(event.jid, sizeof(event.jid), "%s", id);
    i_push_event(backend, &event);
}

static void i_process_message(TelegramBackend *backend, const char_t *json)
{
    const char_t *message;
    const char_t *content;
    const char_t *text_object;
    char_t chat_id[128];
    char_t text[256];
    CalTelegramEvent event;

    message = strstr(json, "\"message\"");
    if (message == NULL)
        return;
    if (i_json_bool(message, "is_outgoing") == TRUE)
        return;
    if (i_json_string(message, "chat_id", chat_id, sizeof(chat_id)) == FALSE
        || chat_id[0] == '\0')
        return;
    content = strstr(message, "\"content\"");
    if (content == NULL || i_has(content, "messageText") == FALSE)
        return;
    text_object = strstr(content, "\"text\"");
    if (text_object == NULL
        || i_json_string(text_object, "text", text, sizeof(text)) == FALSE
        || text[0] == '\0')
        return;

    memset(&event, 0, sizeof(event));
    event.type = ekCAL_TG_EVENT_MESSAGE;
    bstd_sprintf(event.jid, sizeof(event.jid), "%s", chat_id);
    bstd_sprintf(event.text, sizeof(event.text), "%s", text);
    i_push_event(backend, &event);
}

static void i_request_chats(struct TdJson *client, const char_t *json)
{
    const char_t *p;
    char_t request[512];
    char_t id[64];
    uint32_t n;

    p = i_find_key(json, "chat_ids");
    if (p == NULL)
        return;
    p = strchr(p, '[');
    if (p == NULL)
        return;

    while (*p != '\0' && *p != ']')
    {
        if (*p != '-' && (*p < '0' || *p > '9'))
        {
            p += 1;
            continue;
        }

        n = 0;
        if (*p == '-' && n + 1 < sizeof(id))
            id[n++] = *p++;
        while (*p >= '0' && *p <= '9' && n + 1 < sizeof(id))
            id[n++] = *p++;
        id[n] = '\0';
        if (id[0] != '\0')
        {
            bstd_sprintf(request, sizeof(request),
                         "{\"@type\":\"getChat\",\"chat_id\":\"%s\"}", id);
            td_json_client_send(client, request);
        }
    }
}

static void i_process_response(TelegramBackend *backend, struct TdJson *client,
                               const char_t *json)
{
    char_t type[80];
    char_t state[80];

    if (json == NULL || json[0] == '\0')
        return;
    state[0] = '\0';
    if (i_auth_state(json, state, sizeof(state)) == FALSE
        && strstr(json, "authorizationStateWaitTdlibParameters") != NULL)
        bstd_sprintf(state, sizeof(state), "%s", "authorizationStateWaitTdlibParameters");
    if (state[0] != '\0')
    {
        if (strcmp(state, "authorizationStateReady") == 0)
        {
            bmutex_lock(backend->mutex);
            backend->connected = TRUE;
            bmutex_unlock(backend->mutex);
            i_connection_event(backend, "Telegram connected");
            i_send_ready_requests(backend, client);
        }
        else
        {
            if (strcmp(state, "authorizationStateWaitTdlibParameters") == 0)
            {
                if (backend->parameters_sent == TRUE)
                    return;
                backend->parameters_sent = TRUE;
            }
            if (i_send_auth_request(client, state, backend->identity, backend->password,
                                    backend->api_id, backend->api_hash,
                                    backend->database) == FALSE)
                i_connection_event(backend, "Telegram login code or password required");
        }
        return;
    }

    if (i_json_string(json, "@type", type, sizeof(type)) == FALSE)
        return;
    if (strcmp(type, "updateNewChat") == 0 || strcmp(type, "chat") == 0)
        i_process_chat(backend, json);
    else if (strcmp(type, "chats") == 0)
        i_request_chats(client, json);
    else if (strcmp(type, "updateNewMessage") == 0)
        i_process_message(backend, json);
}

static void i_send_message(struct TdJson *client, const CalTelegramOut *item)
{
    char_t chat_id[256];
    char_t text[2048];
    char_t request[2560];

    i_json_escape(chat_id, sizeof(chat_id), item->jid);
    i_json_escape(text, sizeof(text), item->text);
    bstd_sprintf(request, sizeof(request),
                 "{\"@type\":\"sendMessage\",\"chat_id\":\"%s\","
                 "\"input_message_content\":{\"@type\":\"inputMessageText\","
                 "\"text\":{\"@type\":\"formattedText\",\"text\":\"%s\"},"
                 "\"disable_web_page_preview\":false}}",
                 chat_id, text);
    td_json_client_send(client, request);
}

static void i_send_chat(struct TdJson *client, const CalTelegramOut *item)
{
    char_t chat_id[256];
    char_t request[512];

    i_json_escape(chat_id, sizeof(chat_id), item->jid);
    bstd_sprintf(request, sizeof(request),
                 "{\"@type\":\"getChat\",\"chat_id\":\"%s\"}", chat_id);
    td_json_client_send(client, request);
}

static void i_process_outgoing(TelegramBackend *backend, struct TdJson *client)
{
    CalTelegramOut item;

    while (i_pop_out(backend, &item) == TRUE)
    {
        if (item.type == ekCAL_TG_OUT_MESSAGE)
            i_send_message(client, &item);
        else if (item.type == ekCAL_TG_OUT_CHAT)
            i_send_chat(client, &item);
    }
}

static uint32_t i_thread_main(TelegramBackend *backend)
{
    struct TdJson *client;
    bool_t running = TRUE;

    client = td_json_client_create();
    if (client == NULL)
    {
        i_connection_event(backend, "Could not create Telegram client");
        return 1;
    }

    bmutex_lock(backend->mutex);
    backend->client = client;
    backend->connected = FALSE;
    backend->ready_sent = FALSE;
    backend->parameters_sent = FALSE;
    bmutex_unlock(backend->mutex);

    td_json_client_send(client, "{\"@type\":\"getAuthorizationState\"}");

    while (running == TRUE)
    {
        const char_t *response = td_json_client_receive(client, 0.25);

        if (response != NULL)
            i_process_response(backend, client, response);
        i_process_outgoing(backend, client);
        bmutex_lock(backend->mutex);
        running = backend->running;
        bmutex_unlock(backend->mutex);
    }

    bmutex_lock(backend->mutex);
    backend->client = NULL;
    backend->connected = FALSE;
    bmutex_unlock(backend->mutex);

    td_json_client_destroy(client);
    i_connection_event(backend, "Telegram disconnected");
    return 0;
}

static void i_stop_thread(TelegramBackend *backend)
{
    if (backend == NULL || backend->thread == NULL)
        return;

    bmutex_lock(backend->mutex);
    backend->running = FALSE;
    bmutex_unlock(backend->mutex);

    bthread_wait(backend->thread);
    bthread_close(&backend->thread);
}

typedef struct _tg_test_state_t TgTestState;

struct _tg_test_state_t
{
    bool_t ready;
    bool_t waiting;
    bool_t parameters_sent;
    time_t deadline;
};

static void i_test_step(TgTestState *state, struct TdJson *client, const char_t *json,
                        const char_t *identity, const char_t *password,
                        const char_t *api_id, const char_t *api_hash,
                        const char_t *database_directory,
                        CalTestLog log, void *context)
{
    char_t auth_state[80];
    char_t line[320];

    if (log != NULL && json != NULL
        && (strstr(json, "authorization") != NULL
            || strstr(json, "\"@type\":\"error\"") != NULL))
    {
        bstd_sprintf(line, sizeof(line), "TDLib response: %s", json);
        log(line, context);
    }
    if (i_auth_state(json, auth_state, sizeof(auth_state)) == FALSE)
    {
        if (json != NULL && strstr(json, "authorizationStateWaitTdlibParameters") != NULL)
            bstd_sprintf(auth_state, sizeof(auth_state), "%s",
                         "authorizationStateWaitTdlibParameters");
        else
        {
            if (json != NULL && strstr(json, "\"@type\":\"error\"") != NULL)
            {
                state->waiting = TRUE;
                if (log != NULL)
                {
                    bstd_sprintf(line, sizeof(line), "TDLib error: %s", json);
                    log(line, context);
                }
            }
            return;
        }
    }
    if (log != NULL)
    {
        bstd_sprintf(line, sizeof(line), "TDLib state: %s", auth_state);
        log(line, context);
    }
    if (strcmp(auth_state, "authorizationStateReady") == 0)
    {
        state->ready = TRUE;
        return;
    }
    if (strcmp(auth_state, "authorizationStateWaitTdlibParameters") == 0)
    {
        if (state->parameters_sent == TRUE)
            return;
        state->parameters_sent = TRUE;
        if (log != NULL)
            log("Sending setTdlibParameters.", context);
    }
    if (i_send_auth_request(client, auth_state, identity, password, api_id, api_hash,
                            database_directory) == FALSE)
    {
        state->waiting = TRUE;
        if (log != NULL)
            log("TDLib is waiting for a login code or password.", context);
    }
    else if (log != NULL
             && (strcmp(auth_state, "authorizationStateWaitPhoneNumber") == 0
                 || strcmp(auth_state, "authorizationStateWaitBotToken") == 0))
        log("Identity submitted to TDLib; waiting for authorization.", context);
}

#endif

TelegramBackend *cal_telegram_create(void)
{
    TelegramBackend *backend = heap_new0(TelegramBackend);

    backend->mutex = bmutex_create();
    backend->status = ekCAL_AVAILABLE;
    bstd_sprintf(backend->status_text, sizeof(backend->status_text), "%s", "Available");
    return backend;
}

void cal_telegram_destroy(TelegramBackend **backend)
{
    if (backend == NULL || *backend == NULL)
        return;

#if CALANTAS_HAVE_TELEGRAM
    i_stop_thread(*backend);
#endif
    bmutex_close(&(*backend)->mutex);
    heap_delete(backend, TelegramBackend);
}

bool_t cal_telegram_available(void)
{
#if CALANTAS_HAVE_TELEGRAM
    return TRUE;
#else
    return FALSE;
#endif
}

bool_t cal_telegram_connect(TelegramBackend *backend, const char_t *identity,
                            const char_t *password, const char_t *api_id,
                            const char_t *api_hash)
{
    if (backend == NULL || identity == NULL || identity[0] == '\0'
        || i_valid_api_id_text(api_id) == FALSE
        || api_hash == NULL || api_hash[0] == '\0')
        return FALSE;

#if CALANTAS_HAVE_TELEGRAM
    char_t database[96];

    i_stop_thread(backend);
    i_test_database(database, sizeof(database), identity, api_id, api_hash);

    bmutex_lock(backend->mutex);
    bstd_sprintf(backend->identity, sizeof(backend->identity), "%s", identity);
    bstd_sprintf(backend->password, sizeof(backend->password), "%s",
                 password != NULL ? password : "");
    bstd_sprintf(backend->api_id, sizeof(backend->api_id), "%s", api_id);
    bstd_sprintf(backend->api_hash, sizeof(backend->api_hash), "%s", api_hash);
    bstd_sprintf(backend->database, sizeof(backend->database), "%s", database);
    backend->connected = FALSE;
    backend->ready_sent = FALSE;
    backend->parameters_sent = FALSE;
    backend->running = TRUE;
    backend->event_head = 0;
    backend->event_tail = 0;
    backend->out_head = 0;
    backend->out_tail = 0;
    bmutex_unlock(backend->mutex);

    backend->thread = bthread_create(i_thread_main, backend, TelegramBackend);
    if (backend->thread == NULL)
    {
        bmutex_lock(backend->mutex);
        backend->running = FALSE;
        bmutex_unlock(backend->mutex);
        i_connection_event(backend, "Could not start Telegram worker");
        return FALSE;
    }

    return TRUE;
#else
    i_connection_event(backend, "Telegram unavailable: build with TDLib");
    return FALSE;
#endif
}

bool_t cal_telegram_test_connection_logged(const char_t *identity, const char_t *password,
                                           const char_t *api_id, const char_t *api_hash,
                                           CalTestLog log, void *context)
{
#if CALANTAS_HAVE_TELEGRAM
    struct TdJson *client;
    TgTestState state;
    char_t database[96];
    char_t line[192];
    bool_t success = FALSE;

    if (identity == NULL || identity[0] == '\0'
        || i_valid_api_id_text(api_id) == FALSE
        || api_hash == NULL || api_hash[0] == '\0')
        return FALSE;

    i_test_database(database, sizeof(database), identity, api_id, api_hash);
    memset(&state, 0, sizeof(state));
    state.deadline = time(NULL) + 10;
    client = td_json_client_create();
    if (client == NULL)
        return FALSE;

    if (log != NULL)
    {
        bstd_sprintf(line, sizeof(line), "Test database: %s", database);
        log(line, context);
    }
    td_json_client_send(client, "{\"@type\":\"getAuthorizationState\"}");
    while (state.ready == FALSE && state.waiting == FALSE && time(NULL) < state.deadline)
    {
        const char_t *response = td_json_client_receive(client, 0.1);

        if (response != NULL)
            i_test_step(&state, client, response, identity, password, api_id, api_hash,
                        database, log, context);
    }

    success = state.ready;
    td_json_client_destroy(client);
    return success;
#else
    unref(identity);
    unref(password);
    unref(api_id);
    unref(api_hash);
    unref(log);
    unref(context);
    return FALSE;
#endif
}

bool_t cal_telegram_test_connection(const char_t *identity, const char_t *password,
                                    const char_t *api_id, const char_t *api_hash)
{
    return cal_telegram_test_connection_logged(identity, password, api_id, api_hash,
                                               NULL, NULL);
}

bool_t cal_telegram_is_connected(const TelegramBackend *backend)
{
#if CALANTAS_HAVE_TELEGRAM
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

bool_t cal_telegram_send(TelegramBackend *backend, const char_t *chat_id, const char_t *text)
{
    CalTelegramOut item;

    if (backend == NULL || chat_id == NULL || chat_id[0] == '\0'
        || text == NULL || text[0] == '\0')
        return FALSE;
    if (cal_telegram_available() == FALSE || cal_telegram_is_connected(backend) == FALSE)
        return FALSE;

    memset(&item, 0, sizeof(item));
    item.type = ekCAL_TG_OUT_MESSAGE;
    bstd_sprintf(item.jid, sizeof(item.jid), "%s", chat_id);
    bstd_sprintf(item.text, sizeof(item.text), "%s", text);
    return i_push_out(backend, &item);
}

bool_t cal_telegram_set_status(TelegramBackend *backend, const cal_status_t status,
                               const char_t *text)
{
    if (backend == NULL)
        return FALSE;

    bmutex_lock(backend->mutex);
    backend->status = status;
    bstd_sprintf(backend->status_text, sizeof(backend->status_text), "%s",
                 text != NULL && text[0] != '\0' ? text : "No status message");
    bmutex_unlock(backend->mutex);
    return TRUE;
}

bool_t cal_telegram_add_buddy(TelegramBackend *backend, const char_t *chat_id,
                              const char_t *name)
{
    CalTelegramOut item;
    size_t i;

    if (backend == NULL || chat_id == NULL || chat_id[0] == '\0')
        return FALSE;
    i = 0;
    if (chat_id[i] == '-')
        i += 1;
    if (chat_id[i] == '\0')
        return FALSE;
    for (; chat_id[i] != '\0'; i += 1)
    {
        if (chat_id[i] < '0' || chat_id[i] > '9')
            return FALSE;
    }
    if (cal_telegram_available() == FALSE || cal_telegram_is_connected(backend) == FALSE)
        return FALSE;

    memset(&item, 0, sizeof(item));
    item.type = ekCAL_TG_OUT_CHAT;
    bstd_sprintf(item.jid, sizeof(item.jid), "%s", chat_id);
    bstd_sprintf(item.name, sizeof(item.name), "%s", name != NULL ? name : "");
    return i_push_out(backend, &item);
}

bool_t cal_telegram_disconnect(TelegramBackend *backend)
{
#if CALANTAS_HAVE_TELEGRAM
    if (backend == NULL)
        return FALSE;
    i_stop_thread(backend);
    return TRUE;
#else
    unref(backend);
    return FALSE;
#endif
}
