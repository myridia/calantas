#include "gui.h"

#include "backend.h"

#include <nappgui.h>
#include <sewer/bstd.h>

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(CALANTAS_TELEGRAM_APP_API_ID) && defined(CALANTAS_TELEGRAM_APP_API_HASH)
#define CALANTAS_HAVE_BAKED_TELEGRAM_APP 1
#endif

#define CAL_MAX_CHATS CALANTAS_MAX_BUDDIES

typedef struct _chat_t Chat;
typedef struct _account_t Account;
typedef struct _accountmanager_t AccountManager;
typedef struct _buddyform_t BuddyForm;
typedef struct _about_t About;

static void i_build_menu(App *app);

struct _chat_t
{
    App *app;
    Window *window;
    TextView *text;
    Edit *input;
    uint32_t index;
    uint32_t rendered;
    uint32_t emotion;
    char_t handle[128];
};

struct _account_t
{
    App *app;
    Window *window;
    Button *bot_mode;
    const char_t *protocol;
    Edit *name;
    Edit *jid;
    Edit *password;
    Edit *server;
    Edit *status;
    Button *auto_connect;
    Label *identity_label;
    Label *password_label;
    Label *server_label;
    Label *result;
    TextView *log;
    char_t api_id[32];
    char_t api_hash[64];
    char_t original[64];
    bool_t is_new;
    bool_t accepted;
};

struct _accountmanager_t
{
    App *app;
    Window *window;
    TableView *table;
    Label *status;
    CalAccount accounts[CALANTAS_MAX_ACCOUNTS];
    char_t labels[CALANTAS_MAX_ACCOUNTS][80];
    uint32_t count;
    uint32_t selected;
};

struct _buddyform_t
{
    App *app;
    Window *window;
    Edit *name;
    Edit *handle;
    bool_t accepted;
};

struct _about_t
{
    Window *window;
};

struct _app_t
{
    Window *window;
    TableView *table;
    Edit *filter;
    Label *status;
    Button *message;
    Button *add;
    uint32_t *rows;
    uint32_t nrows;
    char_t search[128];
    bool_t show_offline;
    Backend *backend;
    Chat *chats[CAL_MAX_CHATS];
};

static void i_account_log(Account *form, const char_t *text)
{
    char_t line[640];
    char_t stamp[32];
    struct tm *nowtm;
    time_t now;

    if (form == NULL || form->log == NULL || text == NULL)
        return;
    now = time(NULL);
    nowtm = localtime(&now);
    if (nowtm != NULL)
        strftime(stamp, sizeof(stamp), "%H:%M:%S", nowtm);
    else
        bstd_sprintf(stamp, sizeof(stamp), "--:--:--");
    bstd_sprintf(line, sizeof(line), "[%s] %s\n", stamp, text);
    textview_printf(form->log, "%s", line);
    textview_scroll_caret(form->log);
}

static void i_account_test_log(const char_t *text, void *context)
{
    i_account_log((Account *)context, text);
}

static void i_mask_identity(const char_t *text, char_t *out, const uint32_t size)
{
    size_t length;

    if (out == NULL || size == 0)
        return;
    out[0] = '\0';
    if (text == NULL)
        return;
    length = strlen(text);
    if (length <= 6)
        bstd_sprintf(out, size, "%s", text);
    else
        bstd_sprintf(out, size, "%.3s***%s", text, text + length - 4);
}

static bool_t i_valid_telegram_api_id(const char_t *text)
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

static bool_t i_valid_telegram_api_hash(const char_t *text)
{
    size_t i;
    size_t length;

    if (text == NULL)
        return FALSE;
    length = strlen(text);
    if (length != 32)
        return FALSE;
    for (i = 0; i < length; i += 1)
    {
        if (isxdigit((unsigned char)text[i]) == 0)
            return FALSE;
    }
    return TRUE;
}

static bool_t i_telegram_app_credentials(App *app, char_t *api_id, const uint32_t id_size,
                                         char_t *api_hash, const uint32_t hash_size)
{
    CalAccount accounts[CALANTAS_MAX_ACCOUNTS];
    uint32_t i;
    uint32_t count;

    if (api_id == NULL || api_hash == NULL)
        return FALSE;

#if defined(CALANTAS_HAVE_BAKED_TELEGRAM_APP)
    if (i_valid_telegram_api_id(CALANTAS_TELEGRAM_APP_API_ID) == TRUE
        && i_valid_telegram_api_hash(CALANTAS_TELEGRAM_APP_API_HASH) == TRUE)
    {
        bstd_sprintf(api_id, id_size, "%s", CALANTAS_TELEGRAM_APP_API_ID);
        bstd_sprintf(api_hash, hash_size, "%s", CALANTAS_TELEGRAM_APP_API_HASH);
        return TRUE;
    }
#endif

    count = cal_backend_accounts(accounts, CALANTAS_MAX_ACCOUNTS);
    for (i = 0; i < count; i += 1)
    {
        if (strcmp(accounts[i].protocol, "Telegram") == 0
            && i_valid_telegram_api_id(accounts[i].api_id) == TRUE
            && i_valid_telegram_api_hash(accounts[i].api_hash) == TRUE)
        {
            bstd_sprintf(api_id, id_size, "%s", accounts[i].api_id);
            bstd_sprintf(api_hash, hash_size, "%s", accounts[i].api_hash);
            return TRUE;
        }
    }

    if (app != NULL && strcmp(cal_backend_protocol(app->backend), "Telegram") == 0
        && i_valid_telegram_api_id(cal_backend_api_id(app->backend)) == TRUE
        && i_valid_telegram_api_hash(cal_backend_api_hash(app->backend)) == TRUE)
    {
        bstd_sprintf(api_id, id_size, "%s", cal_backend_api_id(app->backend));
        bstd_sprintf(api_hash, hash_size, "%s", cal_backend_api_hash(app->backend));
        return TRUE;
    }

    return FALSE;
}

static void i_set_status(App *app, const char_t *text)
{
    char_t line[256];
    char_t combined[512];
    const char_t *jid;
    const char_t *marker;
    const char_t *name;

    cassert_no_null(app);
    jid = cal_backend_account_jid(app->backend);
    marker = cal_backend_active_account(app->backend);
    name = cal_backend_account_name(app->backend);
    if (marker != NULL && marker[0] != '\0' && strcmp(marker, name) != 0)
    {
        if (jid != NULL && jid[0] != '\0')
            bstd_sprintf(line, sizeof(line), "%s [%s] <%s> - %s", name, marker, jid,
                         cal_backend_status_text(app->backend));
        else
            bstd_sprintf(line, sizeof(line), "%s [%s] - %s", name, marker,
                         cal_backend_status_text(app->backend));
    }
    else if (jid != NULL && jid[0] != '\0')
    {
        bstd_sprintf(line, sizeof(line), "%s <%s> - %s", name, jid,
                     cal_backend_status_text(app->backend));
    }
    else
    {
        bstd_sprintf(line, sizeof(line), "%s - %s", name,
                     cal_backend_status_text(app->backend));
    }
    bstd_sprintf(combined, sizeof(combined), "%s\n%s", line, text != NULL ? text : "");
    label_text(app->status, combined);
}

static uint32_t i_chat_index(App *app, Chat *chat)
{
    const CalBuddy *buddy;
    uint32_t i;

    if (chat == NULL)
        return CAL_MAX_CHATS;

    if (chat->handle[0] != '\0')
    {
        for (i = 0; i < cal_backend_buddy_count(app->backend); i += 1)
        {
            buddy = cal_backend_buddy(app->backend, i);
            if (buddy != NULL && strcmp(buddy->handle, chat->handle) == 0)
            {
                chat->index = i;
                return i;
            }
        }
    }

    if (chat->index < cal_backend_buddy_count(app->backend))
        return chat->index;
    return CAL_MAX_CHATS;
}

static void i_remove_chat(App *app, Chat *chat)
{
    uint32_t i;

    if (chat == NULL)
        return;

    for (i = 0; i < CAL_MAX_CHATS; i += 1)
    {
        if (app->chats[i] == chat)
        {
            window_destroy(&chat->window);
            heap_delete(&app->chats[i], Chat);
            return;
        }
    }
}

static bool_t i_contains_ci(const char_t *haystack, const char_t *needle)
{
    size_t nlen;
    const char_t *h;

    if (haystack == NULL || needle == NULL)
        return FALSE;

    nlen = strlen(needle);
    if (nlen == 0)
        return TRUE;

    for (h = haystack; *h != '\0'; h += 1)
    {
        size_t i = 0;
        while (i < nlen && h[i] != '\0' &&
               tolower((unsigned char)h[i]) == tolower((unsigned char)needle[i]))
            i += 1;
        if (i == nlen)
            return TRUE;
    }

    return FALSE;
}

static void i_rebuild_rows(App *app)
{
    uint32_t i;
    uint32_t count = 0;
    uint32_t total = cal_backend_buddy_count(app->backend);

    if (app->rows != NULL)
    {
        free(app->rows);
        app->rows = NULL;
    }
    app->nrows = 0;

    if (total == 0)
        return;

    app->rows = (uint32_t *)malloc(total * sizeof(uint32_t));
    if (app->rows == NULL)
    {
        i_set_status(app, "Out of memory while building the buddy list.");
        return;
    }

    for (i = 0; i < total; i += 1)
    {
        const CalBuddy *buddy = cal_backend_buddy(app->backend, i);
        if (app->show_offline == FALSE && buddy->status == ekCAL_OFFLINE)
            continue;
        if (app->search[0] == '\0' || i_contains_ci(buddy->name, app->search) ||
            i_contains_ci(buddy->handle, app->search))
            app->rows[count++] = i;
    }

    app->nrows = count;
}

static void i_refresh_table(App *app)
{
    tableview_update(app->table);
}

static const char_t *i_status_mark(const cal_status_t status)
{
    switch (status)
    {
    case ekCAL_AVAILABLE:
        return "[+]";
    case ekCAL_AWAY:
        return "[~]";
    case ekCAL_BUSY:
        return "[x]";
    case ekCAL_OFFLINE:
    default:
        return "[ ]";
    }
}

static void i_apply_status(App *app, const cal_status_t status, const char_t *text)
{
    cal_backend_set_status(app->backend, status, text);
    cal_backend_store(app->backend);
    i_rebuild_rows(app);
    i_refresh_table(app);
    i_set_status(app, text);
}

static const CalBuddy *i_row_buddy(App *app, const uint32_t row)
{
    if (row >= app->nrows)
        return NULL;
    return cal_backend_buddy(app->backend, app->rows[row]);
}

static void i_OnTableData(App *app, Event *e)
{
    switch (event_type(e))
    {
    case ekGUI_EVENT_TBL_NROWS:
        *event_result(e, uint32_t) = app->nrows;
        break;

    case ekGUI_EVENT_TBL_CELL:
    {
        const EvTbPos *pos = event_params(e, EvTbPos);
        EvTbCell *cell = event_result(e, EvTbCell);
        const CalBuddy *buddy = i_row_buddy(app, pos->row);

        cell->align = ekLEFT;
        cell->text = "";

        if (buddy != NULL)
        {
            switch (pos->col)
            {
            case 0:
                cell->text = i_status_mark(buddy->status);
                cell->align = ekCENTER;
                break;
            case 1:
                cell->text = buddy->name;
                break;
            case 2:
                cell->text = buddy->handle;
                break;
            case 3:
                cell->text = buddy->status_text;
                break;
            default:
                break;
            }
        }
        break;
    }

    default:
        break;
    }
}

static void i_refresh_chat(Chat *chat)
{
    uint32_t i;
    uint32_t index = i_chat_index(chat->app, chat);
    uint32_t count;

    if (index >= CAL_MAX_CHATS)
        return;

    chat->index = index;
    count = cal_backend_message_count(chat->app->backend, index);

    for (i = chat->rendered; i < count; i += 1)
    {
        const CalMessage *message = cal_backend_message(chat->app->backend, chat->index, i);
        if (message != NULL)
            textview_printf(chat->text, "[%s] %s: %s\n", message->time, message->sender,
                            message->text);
    }

    chat->rendered = count;
    textview_scroll_caret(chat->text);
}

static void i_OnChatEmotion(Chat *chat, Event *e)
{
    static const char_t *emotions[] = {
        "🙂", "😀", "😄", "😊", "😉", "😍", "😂", "😎", "😢", "😡", "👍", "❤️"
    };
    const uint32_t count = (uint32_t)(sizeof(emotions) / sizeof(emotions[0]));
    const char_t *current = edit_get_text(chat->input);
    char_t text[2048];

    bstd_sprintf(text, sizeof(text), "%s%s%s", current != NULL ? current : "",
                 current != NULL && current[0] != '\0' ? " " : "", emotions[chat->emotion % count]);
    edit_text(chat->input, text);
    chat->emotion = (chat->emotion + 1) % count;
    window_focus(chat->window, guicontrol(chat->input));
    unref(e);
}

static void i_OnChatSend(Chat *chat, Event *e)
{
    const char_t *text = edit_get_text(chat->input);
    uint32_t index = i_chat_index(chat->app, chat);

    if (index >= CAL_MAX_CHATS)
    {
        i_set_status(chat->app, "That buddy is no longer available.");
        unref(e);
        return;
    }

    if (text != NULL && text[0] != '\0')
    {
        if (cal_backend_send(chat->app->backend, index, text) == TRUE)
        {
            edit_text(chat->input, "");
            cal_backend_store(chat->app->backend);
            i_refresh_chat(chat);
            i_set_status(chat->app, "Message sent");
        }
        else
        {
            i_set_status(chat->app, "Message could not be sent.");
        }
    }

    unref(e);
}

static void i_OnChatClose(Chat *chat, Event *e)
{
    i_remove_chat(chat->app, chat);
    unref(e);
}

static void i_open_chat(App *app, const uint32_t index)
{
    Chat *chat;
    Panel *panel;
    Layout *layout;
    Layout *toolbar;
    Layout *bottom;
    Button *emotion;
    Button *send;
    char_t title[160];
    const CalBuddy *buddy = cal_backend_buddy(app->backend, index);

    if (buddy == NULL || index >= CAL_MAX_CHATS)
        return;

    if (app->chats[index] != NULL)
    {
        window_show(app->chats[index]->window);
        window_focus(app->chats[index]->window, guicontrol(app->chats[index]->input));
        return;
    }

    chat = heap_new0(Chat);
    chat->app = app;
    chat->index = index;
    bstd_sprintf(chat->handle, sizeof(chat->handle), "%s", buddy->handle);
    chat->window = window_create(ekWINDOW_STDRES);
    chat->text = textview_create();
    chat->input = edit_multiline();
    emotion = button_push();
    send = button_push();

    bstd_sprintf(title, sizeof(title), "Conversation with %s", buddy->name);
    window_title(chat->window, title);
    window_origin(chat->window, v2df(260, 180));
    window_OnClose(chat->window, listener(chat, i_OnChatClose, Chat));

    textview_size(chat->text, s2df(760, 420));
    edit_height(chat->input, 110);
    edit_phtext(chat->input, "Type a message");
    button_text(emotion, "🙂 Emotions");
    button_width(emotion, 130);
    button_OnClick(emotion, listener(chat, i_OnChatEmotion, Chat));
    button_text(send, "Send");
    button_width(send, 80);
    button_OnClick(send, listener(chat, i_OnChatSend, Chat));

    layout = layout_create(1, 3);
    toolbar = layout_create(1, 1);
    bottom = layout_create(2, 1);
    layout_textview(layout, chat->text, 0, 0);
    layout_layout(layout, toolbar, 0, 1);
    layout_layout(layout, bottom, 0, 2);
    layout_button(toolbar, emotion, 0, 0);
    layout_halign(toolbar, 0, 0, ekLEFT);
    layout_hexpand(toolbar, 0);
    layout_edit(bottom, chat->input, 0, 0);
    layout_button(bottom, send, 1, 0);
    layout_hexpand(bottom, 0);
    layout_valign(bottom, 0, 0, ekTOP);
    layout_valign(bottom, 1, 0, ekTOP);
    layout_hmargin(bottom, 0, 6);
    layout_hexpand(layout, 0);
    layout_vexpand(layout, 0);
    layout_margin(layout, 12);

    panel = panel_create();
    panel_layout(panel, layout);
    window_panel(chat->window, panel);
    window_defbutton(chat->window, send);
    window_client_size(chat->window, s2df(880, 620));

    app->chats[index] = chat;
    i_refresh_chat(chat);
    window_show(chat->window);
    window_focus(chat->window, guicontrol(chat->input));
}

static void i_OnRowClick(App *app, Event *e)
{
    const EvTbRow *p = event_params(e, EvTbRow);

    if (p != NULL && p->row < app->nrows)
        i_open_chat(app, app->rows[p->row]);

    unref(e);
}

static void i_OnFilterChange(App *app, Event *e)
{
    const char_t *text = edit_get_text(app->filter);

    bstd_sprintf(app->search, sizeof(app->search), "%s", text != NULL ? text : "");
    i_rebuild_rows(app);
    tableview_deselect_all(app->table);
    i_refresh_table(app);
    unref(e);
}

static void i_OnMessage(App *app, Event *e)
{
    uint32_t row = 0;
    const ArrSt(uint32_t) *selected = tableview_selected(app->table);

    if (arrst_size(selected, uint32_t) > 0)
        row = *arrst_get_const(selected, 0, uint32_t);

    if (app->nrows == 0)
        i_set_status(app, "No buddy to message.");
    else if (row < app->nrows)
        i_open_chat(app, app->rows[row]);

    unref(e);
}

static uint32_t i_protocol_index(const char_t *protocol)
{
    uint32_t i;

    if (protocol == NULL)
        return 0;
    for (i = 0; protocol[i] != '\0'; i += 1)
    {
        if (tolower((unsigned char)protocol[i]) != "telegram"[i])
            return 0;
    }
    return i == 8 ? 1 : 0;
}

static const char_t *i_protocol_name(const char_t *protocol)
{
    return i_protocol_index(protocol) == 1 ? "Telegram" : "XMPP";
}

static bool_t i_form_telegram(const Account *form)
{
    return form != NULL && form->protocol != NULL
               && i_protocol_index(form->protocol) == 1 ? TRUE : FALSE;
}

static bool_t i_telegram_bot_mode(const Account *form)
{
    return form != NULL && form->bot_mode != NULL
               && button_get_state(form->bot_mode) == ekGUI_ON ? TRUE : FALSE;
}

static void i_update_telegram_fields(Account *form)
{
    bool_t bot = i_telegram_bot_mode(form);

    label_text(form->identity_label, bot == TRUE ? "Bot token:" : "Phone number:");
    edit_phtext(form->jid, bot == TRUE ? "123456:TOKEN" : "+66812345678");
    label_text(form->password_label, bot == TRUE ? "Not used:" : "Login code / 2FA:");
    edit_phtext(form->password, bot == TRUE ? "Leave empty" : "Enter after Test");
    edit_editable(form->password, bot == FALSE);
    if (bot == TRUE)
        edit_text(form->password, "");
    label_text(form->result, bot == TRUE
        ? "Paste the bot token from BotFather, then click Test Connection or Save."
        : "1) Click Test Connection. 2) Enter the code Telegram sends. 3) Click Test Connection again.");
}

static void i_OnTelegramBotChange(Account *form, Event *e)
{
    i_update_telegram_fields(form);
    i_account_log(form, i_telegram_bot_mode(form) == TRUE
                  ? "Identity mode changed to bot token."
                  : "Identity mode changed to phone number.");
    unref(e);
}

static void i_OnTest(Account *form, Event *e)
{
    const char_t *jid = edit_get_text(form->jid);
    const char_t *password = edit_get_text(form->password);
    const char_t *api_id = form->api_id;
    const char_t *api_hash = form->api_hash;
    bool_t telegram = i_form_telegram(form);
    char_t line[320];
    char_t identity[96];

    i_account_log(form, "=== Test Connection clicked ===");
    bstd_sprintf(line, sizeof(line), "Protocol: %s", form->protocol);
    i_account_log(form, line);
    if (telegram == TRUE)
    {
        bstd_sprintf(line, sizeof(line), "Identity mode: %s",
                     i_telegram_bot_mode(form) == TRUE ? "bot token" : "phone number");
        i_account_log(form, line);
        if (i_telegram_bot_mode(form) == TRUE)
            i_account_log(form, "Identity: bot token hidden from log.");
        else
        {
            i_mask_identity(jid, identity, sizeof(identity));
            bstd_sprintf(line, sizeof(line), "Phone: %s", identity);
            i_account_log(form, line);
        }
        i_account_log(form, i_valid_telegram_api_id(api_id) == TRUE
                            && i_valid_telegram_api_hash(api_hash) == TRUE
                                ? "Telegram app credentials: configured."
                                : "Telegram app credentials: missing.");
        bstd_sprintf(line, sizeof(line), "Login code supplied: %s",
                     password != NULL && password[0] != '\0' ? "yes" : "no");
        i_account_log(form, line);
    }
    else
    {
        i_mask_identity(jid, identity, sizeof(identity));
        bstd_sprintf(line, sizeof(line), "JID: %s", identity);
        i_account_log(form, line);
    }

    if (jid == NULL || jid[0] == '\0')
    {
        label_text(form->result, telegram == TRUE
            ? "Enter a phone number or bot token to test."
            : "Enter a JID to test.");
        i_account_log(form, "Stopped: identity is empty.");
    }
    else if (telegram == TRUE)
    {
        if (i_valid_telegram_api_id(api_id) == FALSE)
        {
            label_text(form->result, "This build has no Telegram app API ID. Ask the builder to set telegram-app.json.");
            i_account_log(form, "Stopped: API ID must be digits only and greater than zero.");
        }
        else if (i_valid_telegram_api_hash(api_hash) == FALSE)
        {
            label_text(form->result, "This build has no Telegram app API hash. Ask the builder to set telegram-app.json.");
            i_account_log(form, "Stopped: API hash must be 32 hex characters.");
        }
        else if (cal_backend_telegram_available() == FALSE)
        {
            label_text(form->result, "Telegram support is not available in this build.");
            i_account_log(form, "Stopped: this build has no Telegram backend.");
        }
        else
        {
            bool_t connected;

            label_text(form->result, "Testing connection...");
            i_account_log(form, "Calling TDLib test client; waiting up to 10 seconds.");
            connected = cal_backend_test_connection_logged(form->protocol, jid, password, "",
                                                           api_id, api_hash,
                                                           i_account_test_log, form);
            bstd_sprintf(line, sizeof(line), "Test client returned: %s.",
                         connected == TRUE ? "connected" : "not connected");
            i_account_log(form, line);
            if (connected == TRUE)
            {
                label_text(form->result, "Connected.");
                i_account_log(form, "Telegram test session authorized.");
            }
            else if (i_telegram_bot_mode(form) == TRUE)
            {
                label_text(form->result, "Bot login failed. Check the token and API ID/hash.");
                i_account_log(form, "Bot authorization did not finish. Check token and API ID/hash.");
            }
            else
            {
                label_text(form->result, "Code required? Enter it in Login code / 2FA and click Test Connection again.");
                i_account_log(form, "Waiting for login code. Put it in Login code / 2FA and click Test Connection again.");
            }
        }
    }
    else if (password == NULL || password[0] == '\0')
    {
        label_text(form->result, "Enter JID and password to test.");
        i_account_log(form, "Stopped: password is empty.");
    }
    else if (cal_backend_xmpp_available() == FALSE)
    {
        label_text(form->result, "XMPP support is not available in this build.");
        i_account_log(form, "Stopped: this build has no XMPP backend.");
    }
    else
    {
        const char_t *server = form->server != NULL ? edit_get_text(form->server) : "";
        bool_t connected;

        label_text(form->result, "Testing connection...");
        bstd_sprintf(line, sizeof(line), "Server: %s", server != NULL && server[0] != '\0' ? server : "(default)");
        i_account_log(form, line);
        connected = cal_backend_test_connection_logged(form->protocol, jid, password, server,
                                                       api_id, api_hash,
                                                       i_account_test_log, form);
        bstd_sprintf(line, sizeof(line), "Test client returned: %s.",
                     connected == TRUE ? "connected" : "not connected");
        i_account_log(form, line);
        if (connected == TRUE)
            label_text(form->result, "Connected.");
        else
            label_text(form->result, "Connection failed or timed out.");
    }

    unref(e);
}

static void i_OnAccountOk(Account *form, Event *e)
{
    const char_t *name_text = edit_get_text(form->name);
    const char_t *text = edit_get_text(form->status);
    const char_t *jid_text = edit_get_text(form->jid);
    const char_t *password_text = edit_get_text(form->password);
    const char_t *server_text = form->server != NULL ? edit_get_text(form->server) : "";
    const char_t *api_id_text = form->api_id;
    const char_t *api_hash_text = form->api_hash;
    CalAccount account;
    char_t name[64];
    char_t jid[128];
    char_t password[128];
    char_t server[128];
    char_t api_id[32];
    char_t api_hash[64];
    char_t connecting[128];
    bool_t telegram = i_form_telegram(form);
    bool_t auto_connect = button_get_state(form->auto_connect) == ekGUI_ON;
    App *app = form->app;
    char_t line[320];
    char_t identity[96];

    bstd_sprintf(name, sizeof(name), "%s", name_text != NULL ? name_text : "");
    bstd_sprintf(jid, sizeof(jid), "%s", jid_text != NULL ? jid_text : "");
    bstd_sprintf(password, sizeof(password), "%s", password_text != NULL ? password_text : "");
    bstd_sprintf(server, sizeof(server), "%s", server_text != NULL ? server_text : "");
    bstd_sprintf(api_id, sizeof(api_id), "%s", api_id_text != NULL ? api_id_text : "");
    bstd_sprintf(api_hash, sizeof(api_hash), "%s", api_hash_text != NULL ? api_hash_text : "");
    if (telegram == TRUE && i_telegram_bot_mode(form) == TRUE)
        password[0] = '\0';

    if (name[0] == '\0')
        bstd_sprintf(name, sizeof(name), "%s", form->protocol);

    i_account_log(form, "=== Save clicked ===");
    bstd_sprintf(line, sizeof(line), "Account name: %s", name);
    i_account_log(form, line);
    bstd_sprintf(line, sizeof(line), "Protocol: %s", form->protocol);
    i_account_log(form, line);
    if (telegram == TRUE && i_telegram_bot_mode(form) == TRUE)
        i_account_log(form, "Identity: bot token hidden from log.");
    else
    {
        i_mask_identity(jid, identity, sizeof(identity));
        bstd_sprintf(line, sizeof(line), "Identity: %s", identity);
        i_account_log(form, line);
    }
    bstd_sprintf(line, sizeof(line), "Login code supplied: %s",
                 password[0] != '\0' ? "yes" : "no");
    i_account_log(form, line);
    bstd_sprintf(line, sizeof(line), "Auto-connect: %s",
                 auto_connect == TRUE ? "yes" : "no");
    i_account_log(form, line);

    if (jid[0] == '\0')
    {
        label_text(form->result, telegram == TRUE
            ? "Enter a phone number or bot token before saving."
            : "Enter an identity before saving.");
        i_account_log(form, "Stopped: identity is empty.");
        unref(e);
        return;
    }
    if (telegram == TRUE && i_valid_telegram_api_id(api_id) == FALSE)
    {
        label_text(form->result, "This build has no Telegram app API ID. Ask the builder to set telegram-app.json.");
        i_account_log(form, "Stopped: API ID must be digits only and greater than zero.");
        unref(e);
        return;
    }
    if (telegram == TRUE && i_valid_telegram_api_hash(api_hash) == FALSE)
    {
        label_text(form->result, "This build has no Telegram app API hash. Ask the builder to set telegram-app.json.");
        i_account_log(form, "Stopped: API hash must be 32 hex characters.");
        unref(e);
        return;
    }
    if (telegram == FALSE && password[0] == '\0')
    {
        label_text(form->result, "Enter JID and password before saving.");
        i_account_log(form, "Stopped: password is empty.");
        unref(e);
        return;
    }

    memset(&account, 0, sizeof(account));
    bstd_sprintf(account.name, sizeof(account.name), "%s", name);
    bstd_sprintf(account.protocol, sizeof(account.protocol), "%s", form->protocol);
    bstd_sprintf(account.jid, sizeof(account.jid), "%s", jid);
    bstd_sprintf(account.password, sizeof(account.password), "%s", password);
    bstd_sprintf(account.server, sizeof(account.server), "%s", server);
    bstd_sprintf(account.api_id, sizeof(account.api_id), "%s", api_id);
    bstd_sprintf(account.api_hash, sizeof(account.api_hash), "%s", api_hash);
    account.auto_connect = auto_connect;

    i_account_log(form, form->is_new == TRUE
                  ? "Writing new account to account.json."
                  : "Updating existing account in account.json.");
    if (form->is_new == TRUE)
    {
        if (cal_backend_account_save(&account, TRUE) == FALSE)
        {
            label_text(form->result, "Could not save account.json.");
            i_account_log(form, "Failed to write account.json.");
            unref(e);
            return;
        }
    }
    else
    {
        if (cal_backend_account_replace(form->original, &account, TRUE) == FALSE)
        {
            label_text(form->result, "Could not save account.json.");
            i_account_log(form, "Failed to update account.json.");
            unref(e);
            return;
        }
    }
    i_account_log(form, "account.json written successfully.");

    bstd_sprintf(line, sizeof(line), "Selecting account: %s", name);
    i_account_log(form, line);
    if (cal_backend_select_account(app->backend, name) == TRUE)
        i_rebuild_rows(app);
    cal_backend_set_account_name(app->backend, name);
    cal_backend_set_status(app->backend, cal_backend_status(app->backend), text);
    cal_backend_set_auto_connect(app->backend, auto_connect);
    i_account_log(form, "Starting saved connection worker.");

    if (cal_backend_connect(app->backend, form->protocol, jid, password, server,
                            api_id, api_hash) == TRUE)
    {
        bstd_sprintf(connecting, sizeof(connecting), "%s account saved; connecting", form->protocol);
        i_set_status(app, connecting);
        i_account_log(form, "Connection worker started.");
    }
    else
    {
        i_set_status(app, cal_backend_connection_text(app->backend));
        bstd_sprintf(line, sizeof(line), "Connection worker did not start: %s",
                     cal_backend_connection_text(app->backend));
        i_account_log(form, line);
    }

    {
        char_t saved[128];

        bstd_sprintf(saved, sizeof(saved), "Saved to account.json as %s.", name);
        label_text(form->result, saved);
        i_account_log(form, saved);
    }
    form->accepted = TRUE;
    window_stop_modal(form->window, 1);
    unref(e);
}

static void i_OnAccountCancel(Account *form, Event *e)
{
    form->accepted = FALSE;
    window_stop_modal(form->window, 0);
    unref(e);
}

static bool_t i_open_xmpp_form(App *app, Window *parent, const CalAccount *source,
                               const char_t *original, const bool_t is_new)
{
    Window *window = window_create(ekWINDOW_TITLE | ekWINDOW_CLOSE | ekWINDOW_ESC);
    Layout *layout = layout_create(2, 9);
    Layout *buttons = layout_create(3, 1);
    Label *name_label = label_create();
    Label *jid_label = label_create();
    Label *password_label = label_create();
    Label *server_label = label_create();
    Label *status_label = label_create();
    Label *result_label = label_create();
    Label *result = label_create();
    Edit *name = edit_create();
    Edit *jid = edit_create();
    Edit *password = edit_create();
    Edit *server = edit_create();
    Edit *status = edit_create();
    Button *auto_connect = button_check();
    Button *test = button_push();
    Button *save = button_push();
    Button *cancel = button_push();
    Panel *panel = panel_create();
    CalAccount copy;
    char_t title[128];
    char_t name_value[64];
    char_t jid_value[128];
    char_t password_value[128];
    char_t server_value[128];
    uint32_t i;
    Account form;

    form.app = app;
    form.window = window;
    form.bot_mode = NULL;
    form.protocol = "XMPP";
    form.name = name;
    form.jid = jid;
    form.password = password;
    form.server = server;
    form.status = status;
    form.auto_connect = auto_connect;
    form.identity_label = jid_label;
    form.password_label = password_label;
    form.server_label = server_label;
    form.result = result;
    form.log = NULL;
    bstd_sprintf(form.api_id, sizeof(form.api_id), "%s", "");
    bstd_sprintf(form.api_hash, sizeof(form.api_hash), "%s", "");
    form.is_new = is_new;
    form.accepted = FALSE;
    bstd_sprintf(form.original, sizeof(form.original), "%s",
                 original != NULL ? original : "");

    memset(&copy, 0, sizeof(copy));
    if (source != NULL && source->name[0] != '\0')
        copy = *source;
    else if (is_new == FALSE
             && cal_backend_account_get(cal_backend_active_account(app->backend), &copy) == FALSE)
        memset(&copy, 0, sizeof(copy));
    bstd_sprintf(copy.protocol, sizeof(copy.protocol), "%s", "XMPP");
    if (copy.name[0] == '\0')
        bstd_sprintf(copy.name, sizeof(copy.name), "%s", "XMPP");
    bstd_sprintf(name_value, sizeof(name_value), "%s", copy.name);
    bstd_sprintf(jid_value, sizeof(jid_value), "%s", copy.jid);
    bstd_sprintf(password_value, sizeof(password_value), "%s", copy.password);
    bstd_sprintf(server_value, sizeof(server_value), "%s", copy.server);

    bstd_sprintf(title, sizeof(title), "%s",
                 is_new == TRUE ? "Add XMPP account" : "Edit XMPP account");
    window_title(window, title);
    label_text(name_label, "Account name:");
    label_text(jid_label, "JID:");
    label_text(password_label, "Password:");
    label_text(server_label, "Server:");
    label_text(status_label, "Status:");
    label_text(result_label, "Result:");
    label_text(result, "Not tested");
    label_multiline(result, TRUE);
    label_width(result, 320);
    edit_text(name, name_value);
    edit_text(jid, jid_value);
    edit_text(password, password_value);
    edit_text(server, server_value);
    edit_text(status, cal_backend_status_text(app->backend));
    edit_passmode(password, TRUE);
    button_state(auto_connect, copy.auto_connect == TRUE ? ekGUI_ON : ekGUI_OFF);
    edit_width(name, 320);
    edit_width(jid, 320);
    edit_width(password, 320);
    edit_width(server, 320);
    edit_width(status, 320);
    button_text(auto_connect, "Connect automatically on startup");
    button_text(test, "Test Connection");
    button_text(save, "Save");
    button_text(cancel, "Cancel");
    button_OnClick(test, listener(&form, i_OnTest, Account));
    button_OnClick(save, listener(&form, i_OnAccountOk, Account));
    button_OnClick(cancel, listener(&form, i_OnAccountCancel, Account));

    layout_label(layout, name_label, 0, 0);
    layout_edit(layout, name, 1, 0);
    layout_label(layout, jid_label, 0, 1);
    layout_edit(layout, jid, 1, 1);
    layout_label(layout, password_label, 0, 2);
    layout_edit(layout, password, 1, 2);
    layout_label(layout, server_label, 0, 3);
    layout_edit(layout, server, 1, 3);
    layout_label(layout, status_label, 0, 4);
    layout_edit(layout, status, 1, 4);
    layout_label(layout, result_label, 0, 5);
    layout_label(layout, result, 1, 5);
    layout_button(layout, auto_connect, 1, 6);
    layout_layout(layout, buttons, 1, 7);
    layout_button(buttons, test, 0, 0);
    layout_button(buttons, save, 1, 0);
    layout_button(buttons, cancel, 2, 0);
    layout_hexpand(buttons, 0);
    layout_halign(buttons, 0, 0, ekLEFT);
    layout_hmargin(buttons, 0, 6);
    layout_hmargin(buttons, 1, 6);
    for (i = 0; i < 8; i += 1)
        layout_vmargin(layout, i, 8);
    layout_margin(layout, 14);

    panel_layout(panel, layout);
    window_panel(window, panel);
    window_client_size(window, s2df(560, 440));
    window_defbutton(window, save);
    window_modal(window, parent != NULL ? parent : app->window);
    window_destroy(&window);

    return form.accepted;
}

static bool_t i_open_telegram_form(App *app, Window *parent, const CalAccount *source,
                                   const char_t *original, const bool_t is_new)
{
    Window *window = window_create(ekWINDOW_TITLE | ekWINDOW_CLOSE | ekWINDOW_ESC);
    Layout *layout = layout_create(2, 9);
    Layout *buttons = layout_create(3, 1);
    Label *name_label = label_create();
    Label *identity_label = label_create();
    Label *code_label = label_create();
    Label *status_label = label_create();
    Label *result_label = label_create();
    Label *result = label_create();
    Label *log_label = label_create();
    TextView *log = textview_create();
    Edit *name = edit_create();
    Edit *jid = edit_create();
    Edit *password = edit_create();
    Edit *status = edit_create();
    Button *bot_mode = button_check();
    Button *auto_connect = button_check();
    Button *test = button_push();
    Button *save = button_push();
    Button *cancel = button_push();
    Panel *panel = panel_create();
    CalAccount copy;
    char_t title[128];
    char_t name_value[64];
    char_t jid_value[128];
    char_t password_value[128];
    uint32_t i;
    Account form;

    form.app = app;
    form.window = window;
    form.bot_mode = bot_mode;
    form.protocol = "Telegram";
    form.name = name;
    form.jid = jid;
    form.password = password;
    form.server = NULL;
    form.status = status;
    form.auto_connect = auto_connect;
    form.identity_label = identity_label;
    form.password_label = code_label;
    form.server_label = NULL;
    form.result = result;
    form.log = log;
    form.is_new = is_new;
    form.accepted = FALSE;
    bstd_sprintf(form.original, sizeof(form.original), "%s",
                 original != NULL ? original : "");

    memset(&copy, 0, sizeof(copy));
    if (source != NULL && source->name[0] != '\0')
        copy = *source;
    else if (is_new == FALSE
             && cal_backend_account_get(cal_backend_active_account(app->backend), &copy) == FALSE)
        memset(&copy, 0, sizeof(copy));
    bstd_sprintf(copy.protocol, sizeof(copy.protocol), "%s", "Telegram");
    if (copy.name[0] == '\0')
        bstd_sprintf(copy.name, sizeof(copy.name), "%s", "Telegram");
    bstd_sprintf(name_value, sizeof(name_value), "%s", copy.name);
    bstd_sprintf(jid_value, sizeof(jid_value), "%s", copy.jid);
    bstd_sprintf(password_value, sizeof(password_value), "%s", copy.password);
    bstd_sprintf(form.api_id, sizeof(form.api_id), "%s", copy.api_id);
    bstd_sprintf(form.api_hash, sizeof(form.api_hash), "%s", copy.api_hash);
    if (form.api_id[0] == '\0')
        i_telegram_app_credentials(app, form.api_id, sizeof(form.api_id),
                                   form.api_hash, sizeof(form.api_hash));

    bstd_sprintf(title, sizeof(title), "%s",
                 is_new == TRUE ? "Add Telegram account" : "Edit Telegram account");
    window_title(window, title);
    label_text(name_label, "Account name:");
    label_text(status_label, "Status:");
    label_text(result_label, "Result:");
    label_text(log_label, "Activity log:");
    label_multiline(result, TRUE);
    label_width(result, 480);
    textview_size(log, s2df(480, 240));
    textview_editable(log, FALSE);
    textview_wrap(log, TRUE);
    textview_scroll_visible(log, FALSE, TRUE);
    edit_text(name, name_value);
    edit_text(jid, jid_value);
    edit_text(password, password_value);
    edit_text(status, cal_backend_status_text(app->backend));
    edit_passmode(password, TRUE);
    button_state(bot_mode, strchr(copy.jid, ':') != NULL && strchr(copy.jid, '+') == NULL
                               ? ekGUI_ON : ekGUI_OFF);
    button_state(auto_connect, copy.auto_connect == TRUE ? ekGUI_ON : ekGUI_OFF);
    edit_width(name, 480);
    edit_width(jid, 480);
    edit_width(password, 480);
    edit_width(status, 480);
    button_text(bot_mode, "Use bot token instead of phone number");
    button_text(auto_connect, "Connect automatically on startup");
    button_text(test, "Test Connection");
    button_text(save, "Save");
    button_text(cancel, "Cancel");
    button_OnClick(bot_mode, listener(&form, i_OnTelegramBotChange, Account));
    button_OnClick(test, listener(&form, i_OnTest, Account));
    button_OnClick(save, listener(&form, i_OnAccountOk, Account));
    button_OnClick(cancel, listener(&form, i_OnAccountCancel, Account));
    i_update_telegram_fields(&form);

    layout_label(layout, name_label, 0, 0);
    layout_edit(layout, name, 1, 0);
    layout_label(layout, identity_label, 0, 1);
    layout_edit(layout, jid, 1, 1);
    layout_button(layout, bot_mode, 1, 2);
    layout_label(layout, code_label, 0, 3);
    layout_edit(layout, password, 1, 3);
    layout_label(layout, status_label, 0, 4);
    layout_edit(layout, status, 1, 4);
    layout_label(layout, result_label, 0, 5);
    layout_label(layout, result, 1, 5);
    layout_button(layout, auto_connect, 1, 6);
    layout_layout(layout, buttons, 1, 7);
    layout_label(layout, log_label, 0, 8);
    layout_textview(layout, log, 1, 8);
    layout_button(buttons, test, 0, 0);
    layout_button(buttons, save, 1, 0);
    layout_button(buttons, cancel, 2, 0);
    layout_hexpand(buttons, 0);
    layout_halign(buttons, 0, 0, ekLEFT);
    layout_hmargin(buttons, 0, 6);
    layout_hmargin(buttons, 1, 6);
    for (i = 0; i < 9; i += 1)
        layout_vmargin(layout, i, 8);
    layout_margin(layout, 14);

    panel_layout(panel, layout);
    window_panel(window, panel);
    i_account_log(&form, "Telegram dialog opened.");
    i_account_log(&form, is_new == TRUE
                  ? "Creating a new Telegram account."
                  : "Editing an existing Telegram account.");
    i_account_log(&form, i_telegram_bot_mode(&form) == TRUE
                  ? "Bot mode on: bot token login."
                  : "Bot mode off: phone number login.");
    i_account_log(&form, "Click Test Connection, then enter the login code.");
    window_client_size(window, s2df(780, 900));
    window_defbutton(window, save);
    window_modal(window, parent != NULL ? parent : app->window);
    window_destroy(&window);

    return form.accepted;
}

static void i_manager_refresh(AccountManager *form)
{
    const char_t *active;
    char_t text[200];
    uint32_t i;

    form->count = cal_backend_accounts(form->accounts, CALANTAS_MAX_ACCOUNTS);
    active = cal_backend_active_account(form->app->backend);
    for (i = 0; i < form->count; i += 1)
    {
        const char_t *name = form->accounts[i].name[0] != '\0'
                                 ? form->accounts[i].name
                                 : (form->accounts[i].jid[0] != '\0'
                                        ? form->accounts[i].jid
                                        : i_protocol_name(form->accounts[i].protocol));

        if (active != NULL && active[0] != '\0' && strcmp(active, form->accounts[i].name) == 0)
            bstd_sprintf(form->labels[i], sizeof(form->labels[i]), "* %s", name);
        else
            bstd_sprintf(form->labels[i], sizeof(form->labels[i]), "%s", name);
    }

    form->selected = CALANTAS_MAX_ACCOUNTS;
    tableview_deselect_all(form->table);
    tableview_update(form->table);
    bstd_sprintf(text, sizeof(text),
                 "Active: %s | %u account(s) | Double-click a row to switch",
                 form->count > 0 && active != NULL && active[0] != '\0' ? active : "none",
                 (unsigned int)form->count);
    label_text(form->status, text);
}

static uint32_t i_manager_selected(const AccountManager *form)
{
    const ArrSt(uint32_t) *selected = tableview_selected(form->table);
    uint32_t row;

    if (form->selected < form->count)
        return form->selected;

    row = tableview_get_focus_row(form->table);
    if (row < form->count)
        return row;

    if (selected != NULL && arrst_size(selected, uint32_t) > 0)
    {
        row = *arrst_get_const(selected, 0, uint32_t);
        if (row < form->count)
            return row;
    }

    return CALANTAS_MAX_ACCOUNTS;
}

static void i_manager_focus_row(AccountManager *form, const uint32_t row)
{
    if (row >= form->count)
        return;

    form->selected = row;
    tableview_select(form->table, &row, 1);
    tableview_focus_row(form->table, row, ekCENTER);
    window_focus(form->window, guicontrol(form->table));
}

static void i_manager_activate(AccountManager *form, const uint32_t row)
{
    CalAccount account = form->accounts[row];
    App *app = form->app;

    if (cal_backend_select_account(app->backend, account.name) == TRUE)
    {
        i_rebuild_rows(app);
        if (account.jid[0] != '\0'
            && (i_protocol_index(account.protocol) == 1 || account.password[0] != '\0'))
        {
            if (cal_backend_connect(app->backend, account.protocol, account.jid,
                                    account.password, account.server,
                                    account.api_id, account.api_hash) == TRUE)
            {
                char_t text[96];

                bstd_sprintf(text, sizeof(text), "Connecting to %s",
                             i_protocol_name(account.protocol));
                i_set_status(app, text);
            }
            else
            {
                i_set_status(app, cal_backend_connection_text(app->backend));
            }
        }
        else
        {
            i_set_status(app, "Account needs an identity and password.");
        }
    }
    else
    {
        i_set_status(app, "Could not select account.");
    }

    i_manager_refresh(form);
}

static void i_OnManagerTableData(AccountManager *form, Event *e)
{
    switch (event_type(e))
    {
    case ekGUI_EVENT_TBL_NROWS:
        *event_result(e, uint32_t) = form->count;
        break;

    case ekGUI_EVENT_TBL_CELL:
    {
        const EvTbPos *pos = event_params(e, EvTbPos);
        EvTbCell *cell = event_result(e, EvTbCell);

        cell->align = ekLEFT;
        cell->text = "";
        if (pos != NULL && pos->row < form->count)
        {
            const CalAccount *account = &form->accounts[pos->row];
            switch (pos->col)
            {
            case 0:
                cell->text = form->labels[pos->row];
                break;
            case 1:
                cell->text = i_protocol_name(account->protocol);
                break;
            case 2:
                cell->text = account->jid;
                break;
            case 3:
                cell->text = account->server;
                break;
            case 4:
                cell->text = account->auto_connect == TRUE ? "Yes" : "No";
                break;
            default:
                break;
            }
        }
        break;
    }

    default:
        break;
    }
}

static void i_OnManagerSelect(AccountManager *form, Event *e)
{
    const EvTbSel *sel = event_params(e, EvTbSel);
    char_t text[160];

    if (sel != NULL && sel->sel != NULL && arrst_size(sel->sel, uint32_t) > 0)
    {
        uint32_t row = *arrst_get_const(sel->sel, 0, uint32_t);
        if (row < form->count)
        {
            form->selected = row;
            bstd_sprintf(text, sizeof(text), "Selected: %s | Double-click to switch",
                         form->accounts[row].name);
            label_text(form->status, text);
        }
    }

    unref(e);
}

static void i_OnManagerRowClick(AccountManager *form, Event *e)
{
    static time_t last_click = 0;
    static uint32_t last_row = CALANTAS_MAX_ACCOUNTS;
    const EvTbRow *p = event_params(e, EvTbRow);
    char_t text[160];
    time_t now = time(NULL);

    if (p != NULL && p->row < form->count)
    {
        form->selected = p->row;
        bstd_sprintf(text, sizeof(text), "Selected: %s | Double-click to switch",
                     form->accounts[p->row].name);
        label_text(form->status, text);
        if (last_row == p->row && now - last_click <= 1)
        {
            i_manager_activate(form, p->row);
            last_click = 0;
            last_row = CALANTAS_MAX_ACCOUNTS;
        }
        else
        {
            last_click = now;
            last_row = p->row;
        }
    }

    unref(e);
}

static void i_OnManagerAddXmpp(AccountManager *form, Event *e)
{
    if (i_open_xmpp_form(form->app, form->window, NULL, "", TRUE) == TRUE)
        i_manager_refresh(form);
    unref(e);
}

static void i_OnManagerAddTelegram(AccountManager *form, Event *e)
{
    if (i_open_telegram_form(form->app, form->window, NULL, "", TRUE) == TRUE)
        i_manager_refresh(form);
    unref(e);
}

static void i_OnManagerEdit(AccountManager *form, Event *e)
{
    uint32_t row = i_manager_selected(form);

    if (row == CALANTAS_MAX_ACCOUNTS)
    {
        label_text(form->status, "Select an account to edit.");
    }
    else
    {
        bool_t accepted;
        const CalAccount *account = &form->accounts[row];

        i_manager_focus_row(form, row);
        if (i_protocol_index(account->protocol) == 1)
            accepted = i_open_telegram_form(form->app, form->window, account,
                                            account->name, FALSE);
        else
            accepted = i_open_xmpp_form(form->app, form->window, account,
                                        account->name, FALSE);
        if (accepted == TRUE)
            i_manager_refresh(form);
    }

    unref(e);
}

static void i_OnManagerDelete(AccountManager *form, Event *e)
{
    uint32_t row;

    label_text(form->status, "Deleting...");
    row = i_manager_selected(form);
    if (row == CALANTAS_MAX_ACCOUNTS && form->count > 0)
        row = 0;
    if (row == CALANTAS_MAX_ACCOUNTS)
    {
        label_text(form->status, "Select an account to delete.");
        unref(e);
        return;
    }
    else
    {
        CalAccount accounts[CALANTAS_MAX_ACCOUNTS];
        char_t name[64];
        char_t text[96];
        bool_t was_active;

        i_manager_focus_row(form, row);
        bstd_sprintf(name, sizeof(name), "%s", form->accounts[row].name);
        was_active = strcmp(cal_backend_active_account(form->app->backend), name) == 0;
        if (cal_backend_account_delete(name) == FALSE)
        {
            label_text(form->status, "Could not delete account.");
            unref(e);
            return;
        }

        cal_backend_store(form->app->backend);
        cal_backend_apply_active(form->app->backend);
        if (was_active == TRUE)
        {
            uint32_t count = cal_backend_accounts(accounts, CALANTAS_MAX_ACCOUNTS);
            if (count > 0)
                cal_backend_select_account(form->app->backend, accounts[0].name);
        }
        i_rebuild_rows(form->app);
        i_manager_refresh(form);
        bstd_sprintf(text, sizeof(text), "Deleted %s.", name);
        label_text(form->status, text);
        i_set_status(form->app, "Account deleted");
    }

    unref(e);
}

static void i_OnManagerClose(AccountManager *form, Event *e)
{
    window_stop_modal(form->window, 0);
    unref(e);
}

static void i_OnManageAccounts(App *app, Event *e)
{
    Window *window = window_create(ekWINDOW_TITLE | ekWINDOW_CLOSE | ekWINDOW_ESC);
    Layout *layout = layout_create(4, 3);
    Layout *footer = layout_create(5, 1);
    TableView *table = tableview_create();
    Label *status = label_create();
    Button *add_xmpp = button_push();
    Button *add_telegram = button_push();
    Button *edit = button_push();
    Button *delete_button = button_push();
    Button *close = button_push();
    Panel *panel = panel_create();
    AccountManager form;

    form.app = app;
    form.window = window;
    form.table = table;
    form.status = status;
    form.count = 0;
    form.selected = CALANTAS_MAX_ACCOUNTS;
    memset(form.accounts, 0, sizeof(form.accounts));
    memset(form.labels, 0, sizeof(form.labels));

    window_title(window, "Manage accounts");
    tableview_OnData(table, listener(&form, i_OnManagerTableData, AccountManager));
    tableview_OnSelect(table, listener(&form, i_OnManagerSelect, AccountManager));
    tableview_OnRowClick(table, listener(&form, i_OnManagerRowClick, AccountManager));
    tableview_row_height(table, 26);
    tableview_header_resizable(table, TRUE);
    tableview_header_clickable(table, FALSE);
    tableview_multisel(table, FALSE, FALSE);
    tableview_add_column_text(table);
    tableview_header_title(table, 0, "Account");
    tableview_column_width(table, 0, 140);
    tableview_column_limits(table, 0, 80, 300);
    tableview_add_column_text(table);
    tableview_header_title(table, 1, "Protocol");
    tableview_column_width(table, 1, 90);
    tableview_column_limits(table, 1, 80, 180);
    tableview_add_column_text(table);
    tableview_header_title(table, 2, "JID / identity");
    tableview_column_width(table, 2, 220);
    tableview_column_limits(table, 2, 120, 400);
    tableview_add_column_text(table);
    tableview_header_title(table, 3, "Server");
    tableview_column_width(table, 3, 160);
    tableview_column_limits(table, 3, 100, 360);
    tableview_add_column_text(table);
    tableview_header_title(table, 4, "Auto-connect");
    tableview_column_width(table, 4, 120);
    tableview_column_limits(table, 4, 90, 180);
    tableview_size(table, s2df(750, 220));

    label_text(status, "");
    button_text(add_xmpp, "Add XMPP");
    button_text(add_telegram, "Add Telegram");
    button_text(edit, "Edit");
    button_text(delete_button, "Delete");
    button_text(close, "Close");
    button_width(add_xmpp, 110);
    button_width(add_telegram, 120);
    button_width(edit, 90);
    button_width(delete_button, 90);
    button_width(close, 90);
    button_OnClick(add_xmpp, listener(&form, i_OnManagerAddXmpp, AccountManager));
    button_OnClick(add_telegram, listener(&form, i_OnManagerAddTelegram, AccountManager));
    button_OnClick(edit, listener(&form, i_OnManagerEdit, AccountManager));
    button_OnClick(delete_button, listener(&form, i_OnManagerDelete, AccountManager));
    button_OnClick(close, listener(&form, i_OnManagerClose, AccountManager));

    layout_tableview(layout, table, 0, 0);
    layout_label(layout, status, 0, 1);
    layout_layout(layout, footer, 0, 2);
    layout_button(footer, add_xmpp, 0, 0);
    layout_button(footer, add_telegram, 1, 0);
    layout_button(footer, edit, 2, 0);
    layout_button(footer, delete_button, 3, 0);
    layout_button(footer, close, 4, 0);
    layout_hexpand(layout, 0);
    layout_vexpand(layout, 0);
    layout_hexpand(footer, 0);
    layout_halign(footer, 4, 0, ekRIGHT);
    layout_hmargin(footer, 1, 6);
    layout_hmargin(footer, 2, 6);
    layout_hmargin(footer, 3, 6);
    layout_hmargin(footer, 4, 6);
    layout_vmargin(layout, 0, 8);
    layout_vmargin(layout, 1, 8);
    layout_margin(layout, 14);

    panel_layout(panel, layout);
    window_panel(window, panel);
    window_client_size(window, s2df(830, 360));
    window_defbutton(window, close);
    window_hotkey(window, ekKEY_SUPR, 0, listener(&form, i_OnManagerDelete, AccountManager));
    window_hotkey(window, ekKEY_BACK, 0, listener(&form, i_OnManagerDelete, AccountManager));
    i_manager_refresh(&form);

    window_modal(window, app->window);
    window_destroy(&window);

    unref(e);
}

static void i_OnBuddyOk(BuddyForm *form, Event *e)
{
    const char_t *name = edit_get_text(form->name);
    const char_t *handle = edit_get_text(form->handle);

    form->accepted = cal_backend_add_buddy(form->app->backend, name, handle);
    if (form->accepted == TRUE)
        cal_backend_store(form->app->backend);
    window_stop_modal(form->window, form->accepted == TRUE ? 1 : 0);
    unref(e);
}

static void i_OnBuddyCancel(BuddyForm *form, Event *e)
{
    form->accepted = FALSE;
    window_stop_modal(form->window, 0);
    unref(e);
}

static void i_OnAddBuddy(App *app, Event *e)
{
    Window *window = window_create(ekWINDOW_TITLE | ekWINDOW_CLOSE | ekWINDOW_ESC);
    Layout *layout = layout_create(2, 4);
    Layout *buttons = layout_create(2, 1);
    Label *name_label = label_create();
    Label *handle_label = label_create();
    Edit *name = edit_create();
    Edit *handle = edit_create();
    Button *ok = button_push();
    Button *cancel = button_push();
    Panel *panel = panel_create();
    BuddyForm form;

    form.app = app;
    form.window = window;
    form.name = name;
    form.handle = handle;
    form.accepted = FALSE;

    window_title(window, "Add Buddy");
    label_text(name_label, "Name:");
    label_text(handle_label, "Address:");
    edit_phtext(name, "Buddy name");
    edit_phtext(handle, "user@example.org");
    edit_width(name, 300);
    edit_width(handle, 300);

    button_text(ok, "Add");
    button_text(cancel, "Cancel");
    button_OnClick(ok, listener(&form, i_OnBuddyOk, BuddyForm));
    button_OnClick(cancel, listener(&form, i_OnBuddyCancel, BuddyForm));

    layout_label(layout, name_label, 0, 0);
    layout_edit(layout, name, 1, 0);
    layout_label(layout, handle_label, 0, 1);
    layout_edit(layout, handle, 1, 1);
    layout_layout(layout, buttons, 1, 3);
    layout_button(buttons, ok, 0, 0);
    layout_button(buttons, cancel, 1, 0);
    layout_hexpand(buttons, 0);
    layout_halign(buttons, 0, 0, ekRIGHT);
    layout_hmargin(buttons, 0, 6);
    layout_vmargin(layout, 0, 8);
    layout_vmargin(layout, 1, 8);
    layout_margin(layout, 14);

    panel_layout(panel, layout);
    window_panel(window, panel);
    window_defbutton(window, ok);

    window_modal(window, app->window);
    window_destroy(&window);

    i_rebuild_rows(app);
    i_refresh_table(app);
    i_set_status(app, form.accepted == TRUE ? "Buddy added" : "Buddy not added");
    unref(e);
}

static void i_OnAvailable(App *app, Event *e)
{
    i_apply_status(app, ekCAL_AVAILABLE, "Available");
    unref(e);
}

static void i_OnAway(App *app, Event *e)
{
    i_apply_status(app, ekCAL_AWAY, "Away");
    unref(e);
}

static void i_OnBusy(App *app, Event *e)
{
    i_apply_status(app, ekCAL_BUSY, "Busy");
    unref(e);
}

static void i_OnOffline(App *app, Event *e)
{
    i_apply_status(app, ekCAL_OFFLINE, "Offline");
    unref(e);
}

static void i_OnShowAll(App *app, Event *e)
{
    app->show_offline = TRUE;
    i_rebuild_rows(app);
    i_refresh_table(app);
    unref(e);
}

static void i_OnOnlineOnly(App *app, Event *e)
{
    app->show_offline = FALSE;
    i_rebuild_rows(app);
    i_refresh_table(app);
    unref(e);
}

static void i_OnAboutClose(About *about, Event *e)
{
    window_stop_modal(about->window, 1);
    unref(e);
}

static void i_OnAbout(App *app, Event *e)
{
    Window *window = window_create(ekWINDOW_TITLE | ekWINDOW_CLOSE | ekWINDOW_ESC);
    Layout *layout = layout_create(1, 3);
    Label *title = label_create();
    Label *text = label_create();
    Button *ok = button_push();
    Panel *panel = panel_create();
    About about;

    about.window = window;

    window_title(window, "About Calantas");
    label_text(title, "Calantas Messenger");
    label_text(text, "A basic messenger with the classic Pidgin-style buddy list.\nBuilt with NAppGUI and a custom backend interface. No libpurple3, no GTK.");
    label_multiline(text, TRUE);
    button_text(ok, "Close");
    button_OnClick(ok, listener(&about, i_OnAboutClose, About));

    layout_label(layout, title, 0, 0);
    layout_label(layout, text, 0, 1);
    layout_button(layout, ok, 0, 2);
    layout_halign(layout, 0, 2, ekRIGHT);
    layout_vmargin(layout, 0, 6);
    layout_vmargin(layout, 1, 12);
    layout_margin(layout, 16);

    panel_layout(panel, layout);
    window_panel(window, panel);
    window_defbutton(window, ok);
    window_modal(window, app->window);
    window_destroy(&window);
    unref(e);
}

static void i_OnClose(App *app, Event *e)
{
    osapp_finish();
    unref(app);
    unref(e);
}

static void i_build_menu(App *app)
{
    Menu *menu = menu_create();
    MenuItem *contacts = menuitem_create();
    Menu *contacts_sub = menu_create();
    MenuItem *new_message = menuitem_create();
    MenuItem *add_buddy = menuitem_create();
    MenuItem *online_only = menuitem_create();
    MenuItem *show_all = menuitem_create();
    MenuItem *accounts = menuitem_create();
    Menu *accounts_sub = menu_create();
    MenuItem *account_item = menuitem_create();
    MenuItem *status = menuitem_create();
    Menu *status_sub = menu_create();
    MenuItem *available = menuitem_create();
    MenuItem *away = menuitem_create();
    MenuItem *busy = menuitem_create();
    MenuItem *offline = menuitem_create();
    MenuItem *help = menuitem_create();
    Menu *help_sub = menu_create();
    MenuItem *about = menuitem_create();

    menuitem_text(contacts, "Contacts");
    menuitem_text(new_message, "New Message...");
    menuitem_text(add_buddy, "Add Buddy...");
    menuitem_text(online_only, "Show Online Only");
    menuitem_text(show_all, "Show All Buddies");
    menuitem_OnClick(new_message, listener(app, i_OnMessage, App));
    menuitem_OnClick(add_buddy, listener(app, i_OnAddBuddy, App));
    menuitem_OnClick(online_only, listener(app, i_OnOnlineOnly, App));
    menuitem_OnClick(show_all, listener(app, i_OnShowAll, App));
    menu_add_item(contacts_sub, new_message);
    menu_add_item(contacts_sub, add_buddy);
    menu_add_item(contacts_sub, menuitem_separator());
    menu_add_item(contacts_sub, online_only);
    menu_add_item(contacts_sub, show_all);
    menuitem_submenu(contacts, &contacts_sub);
    menu_add_item(menu, contacts);

    menuitem_text(accounts, "Accounts");
    menuitem_text(account_item, "Manage account");
    menuitem_OnClick(account_item, listener(app, i_OnManageAccounts, App));
    menu_add_item(accounts_sub, account_item);

    menuitem_text(status, "Status");
    menuitem_text(available, "Available");
    menuitem_text(away, "Away");
    menuitem_text(busy, "Busy");
    menuitem_text(offline, "Offline");
    menuitem_OnClick(available, listener(app, i_OnAvailable, App));
    menuitem_OnClick(away, listener(app, i_OnAway, App));
    menuitem_OnClick(busy, listener(app, i_OnBusy, App));
    menuitem_OnClick(offline, listener(app, i_OnOffline, App));
    menu_add_item(status_sub, available);
    menu_add_item(status_sub, away);
    menu_add_item(status_sub, busy);
    menu_add_item(status_sub, offline);
    menuitem_submenu(status, &status_sub);
    menuitem_submenu(accounts, &accounts_sub);
    menu_add_item(menu, accounts);
    menu_add_item(menu, status);

    menuitem_text(help, "Help");
    menuitem_text(about, "About Calantas");
    menuitem_OnClick(about, listener(app, i_OnAbout, App));
    menu_add_item(help_sub, about);
    menuitem_submenu(help, &help_sub);
    menu_add_item(menu, help);

#if !defined(__APPLE__)
    {
        MenuItem *quit = menuitem_create();
        menuitem_text(quit, "Quit");
        menuitem_OnClick(quit, listener(app, i_OnClose, App));
        menu_add_item(menu, quit);
    }
#endif

#if defined(__APPLE__)
    {
        Menu *app_menu = menu_create();
        MenuItem *apple = menuitem_create();
        MenuItem *about_item = menuitem_create();
        MenuItem *quit = menuitem_create();

        menuitem_text(about_item, "About Calantas");
        menuitem_OnClick(about_item, listener(app, i_OnAbout, App));
        menu_add_item(app_menu, about_item);
        menu_add_item(app_menu, menuitem_separator());
        menuitem_text(quit, "Quit Calantas");
        menuitem_OnClick(quit, listener(app, i_OnClose, App));
        menu_add_item(app_menu, quit);
        menuitem_text(apple, "");
        menuitem_submenu(apple, &app_menu);
        menu_ins_item(menu, 0, apple);
    }
#endif

    osapp_menubar(menu, app->window);
}

App *calantas_create(void)
{
    App *app = heap_new0(App);
    Layout *layout = layout_create(1, 3);
    Layout *toolbar = layout_create(5, 1);
    Label *filter_label = label_create();
    Panel *panel;

    app->backend = cal_backend_create();
    app->show_offline = TRUE;
    cal_backend_restore(app->backend);

    app->window = window_create(ekWINDOW_STDRES);
    app->table = tableview_create();
    app->filter = edit_create();
    app->status = label_create();
    app->message = button_push();
    app->add = button_push();

    window_title(app->window, "Calantas Messenger");
    window_origin(app->window, v2df(180, 140));
    window_OnClose(app->window, listener(app, i_OnClose, App));

    button_text(app->message, "Message");
    button_width(app->message, 90);
    button_OnClick(app->message, listener(app, i_OnMessage, App));
    button_text(app->add, "Add Buddy");
    button_width(app->add, 100);
    button_OnClick(app->add, listener(app, i_OnAddBuddy, App));

    label_text(filter_label, "Find:");
    edit_phtext(app->filter, "Search buddies");
    edit_width(app->filter, 220);
    edit_OnChange(app->filter, listener(app, i_OnFilterChange, App));

    tableview_OnData(app->table, listener(app, i_OnTableData, App));
    tableview_OnRowClick(app->table, listener(app, i_OnRowClick, App));
    tableview_row_height(app->table, 26);
    tableview_header_resizable(app->table, TRUE);
    tableview_header_clickable(app->table, FALSE);
    tableview_multisel(app->table, FALSE, FALSE);

    tableview_add_column_text(app->table);
    tableview_header_title(app->table, 0, "");
    tableview_column_width(app->table, 0, 42);
    tableview_column_limits(app->table, 0, 42, 42);

    tableview_add_column_text(app->table);
    tableview_header_title(app->table, 1, "Buddy");
    tableview_column_width(app->table, 1, 220);
    tableview_column_limits(app->table, 1, 120, 400);

    tableview_add_column_text(app->table);
    tableview_header_title(app->table, 2, "Address");
    tableview_column_width(app->table, 2, 220);
    tableview_column_limits(app->table, 2, 120, 400);

    tableview_add_column_text(app->table);
    tableview_header_title(app->table, 3, "Status");
    tableview_column_width(app->table, 3, 260);
    tableview_column_limits(app->table, 3, 120, 500);

    label_multiline(app->status, TRUE);
    i_rebuild_rows(app);

    {
        const char_t *protocol = cal_backend_protocol(app->backend);
        bool_t telegram = i_protocol_index(protocol) == 1;

        if (cal_backend_auto_connect(app->backend) == TRUE
            && cal_backend_account_jid(app->backend)[0] != '\0'
            && (telegram == TRUE || cal_backend_account_password(app->backend)[0] != '\0')
            && (telegram == FALSE || (cal_backend_api_id(app->backend)[0] != '\0'
                                      && cal_backend_api_hash(app->backend)[0] != '\0')))
        {
            if (cal_backend_connect(app->backend, protocol,
                                    cal_backend_account_jid(app->backend),
                                    cal_backend_account_password(app->backend),
                                    cal_backend_account_server(app->backend),
                                    cal_backend_api_id(app->backend),
                                    cal_backend_api_hash(app->backend)) == TRUE)
            {
                char_t text[96];

                bstd_sprintf(text, sizeof(text), "Connecting to %s",
                             i_protocol_name(protocol));
                i_set_status(app, text);
            }
            else
            {
                i_set_status(app, cal_backend_connection_text(app->backend));
            }
        }
        else
        {
            i_set_status(app, "Ready");
        }
    }

    layout_button(toolbar, app->message, 0, 0);
    layout_button(toolbar, app->add, 1, 0);
    layout_label(toolbar, filter_label, 2, 0);
    layout_edit(toolbar, app->filter, 3, 0);
    layout_hexpand(toolbar, 4);
    layout_hmargin(toolbar, 1, 8);
    layout_hmargin(toolbar, 2, 14);
    layout_hmargin(toolbar, 3, 8);

    layout_layout(layout, toolbar, 0, 0);
    layout_tableview(layout, app->table, 0, 1);
    layout_label(layout, app->status, 0, 2);
    layout_vexpand(layout, 1);
    layout_vmargin(layout, 0, 8);
    layout_vmargin(layout, 2, 6);
    layout_margin(layout, 10);

    panel = panel_create();
    panel_layout(panel, layout);
    window_panel(app->window, panel);
    window_client_size(app->window, s2df(880, 620));
    window_hotkey(app->window, ekKEY_N, ekMKEY_COMMAND, listener(app, i_OnMessage, App));
    i_build_menu(app);
    window_show(app->window);

    return app;
}

void calantas_destroy(App **app)
{
    uint32_t i;

    cassert_no_null(app);
    cassert_no_null(*app);

    for (i = 0; i < CAL_MAX_CHATS; i += 1)
    {
        Chat *chat = (*app)->chats[i];
        if (chat != NULL)
        {
            window_destroy(&chat->window);
            heap_delete(&(*app)->chats[i], Chat);
        }
    }

    if ((*app)->rows != NULL)
        free((*app)->rows);

    window_destroy(&(*app)->window);
    cal_backend_store((*app)->backend);
    cal_backend_destroy(&(*app)->backend);
    heap_delete(app, App);
}

void calantas_update(App *app, const real64_t prtime, const real64_t ctime)
{
    CalEvent event;
    uint32_t i;

    while (cal_backend_poll(app->backend, &event) == TRUE)
    {
        i_rebuild_rows(app);
        i_refresh_table(app);
        for (i = 0; i < CAL_MAX_CHATS; i += 1)
        {
            if (app->chats[i] != NULL)
                i_refresh_chat(app->chats[i]);
        }
        cal_backend_store(app->backend);

        if (event.text[0] != '\0')
            i_set_status(app, event.text);
        else if (event.type == ekCAL_EVENT_MESSAGE)
            i_set_status(app, "Message received");
    }

    unref(prtime);
    unref(ctime);
}
