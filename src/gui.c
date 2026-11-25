#include "gui.h"

#include "backend.h"

#include <nappgui.h>
#include <sewer/bstd.h>

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#define CAL_MAX_CHATS CALANTAS_MAX_BUDDIES

typedef struct _chat_t Chat;
typedef struct _account_t Account;
typedef struct _buddyform_t BuddyForm;
typedef struct _about_t About;

struct _chat_t
{
    App *app;
    Window *window;
    TextView *text;
    Edit *input;
    uint32_t index;
    uint32_t rendered;
};

struct _account_t
{
    App *app;
    Window *window;
    Edit *name;
    Edit *status;
    bool_t accepted;
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
    Label *account;
    Button *message;
    Button *add;
    uint32_t *rows;
    uint32_t nrows;
    char_t search[128];
    bool_t show_offline;
    Backend *backend;
    Chat *chats[CAL_MAX_CHATS];
};

static void i_set_status(App *app, const char_t *text)
{
    char_t line[256];

    cassert_no_null(app);
    label_text(app->status, text != NULL ? text : "");
    bstd_sprintf(line, sizeof(line), "%s - %s", cal_backend_account_name(app->backend),
                 cal_backend_status_text(app->backend));
    label_text(app->account, line);
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
    uint32_t count = cal_backend_message_count(chat->app->backend, chat->index);

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

static void i_OnChatSend(Chat *chat, Event *e)
{
    const char_t *text = edit_get_text(chat->input);

    if (text != NULL && text[0] != '\0')
    {
        if (cal_backend_send(chat->app->backend, chat->index, text) == TRUE)
        {
            edit_text(chat->input, "");
            cal_backend_store(chat->app->backend);
            i_refresh_chat(chat);
            i_set_status(chat->app, "Message sent");
        }
        else
        {
            i_set_status(chat->app, "That buddy is offline.");
        }
    }

    unref(e);
}

static void i_OnChatClose(Chat *chat, Event *e)
{
    App *app = chat->app;
    uint32_t index = chat->index;

    window_destroy(&chat->window);
    heap_delete(&app->chats[index], Chat);
    unref(e);
}

static void i_open_chat(App *app, const uint32_t index)
{
    Chat *chat;
    Panel *panel;
    Layout *layout;
    Layout *bottom;
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
    chat->window = window_create(ekWINDOW_STDRES);
    chat->text = textview_create();
    chat->input = edit_create();
    send = button_push();

    bstd_sprintf(title, sizeof(title), "Conversation with %s", buddy->name);
    window_title(chat->window, title);
    window_origin(chat->window, v2df(260, 180));
    window_OnClose(chat->window, listener(chat, i_OnChatClose, Chat));

    textview_size(chat->text, s2df(520, 320));
    edit_width(chat->input, 380);
    edit_phtext(chat->input, "Type a message");
    button_text(send, "Send");
    button_width(send, 80);
    button_OnClick(send, listener(chat, i_OnChatSend, Chat));

    layout = layout_create(1, 2);
    bottom = layout_create(2, 1);
    layout_textview(layout, chat->text, 0, 0);
    layout_layout(layout, bottom, 0, 1);
    layout_edit(bottom, chat->input, 0, 0);
    layout_button(bottom, send, 1, 0);
    layout_hexpand(bottom, 0);
    layout_hmargin(bottom, 0, 6);
    layout_vexpand(layout, 0);
    layout_margin(layout, 10);

    panel = panel_create();
    panel_layout(panel, layout);
    window_panel(chat->window, panel);
    window_defbutton(chat->window, send);
    window_client_size(chat->window, s2df(560, 420));

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

static void i_OnAccountOk(Account *form, Event *e)
{
    const char_t *name = edit_get_text(form->name);
    const char_t *text = edit_get_text(form->status);

    cal_backend_set_account_name(form->app->backend, name);
    cal_backend_set_status(form->app->backend, cal_backend_status(form->app->backend), text);
    cal_backend_store(form->app->backend);
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

static void i_OnAccount(App *app, Event *e)
{
    Window *window = window_create(ekWINDOW_TITLE | ekWINDOW_CLOSE | ekWINDOW_ESC);
    Layout *layout = layout_create(2, 4);
    Layout *buttons = layout_create(2, 1);
    Label *name_label = label_create();
    Label *status_label = label_create();
    Edit *name = edit_create();
    Edit *status = edit_create();
    Button *ok = button_push();
    Button *cancel = button_push();
    Panel *panel = panel_create();
    Account form;

    form.app = app;
    form.window = window;
    form.name = name;
    form.status = status;
    form.accepted = FALSE;

    window_title(window, "Account");
    label_text(name_label, "Name:");
    label_text(status_label, "Status:");
    edit_text(name, cal_backend_account_name(app->backend));
    edit_text(status, cal_backend_status_text(app->backend));
    edit_width(name, 300);
    edit_width(status, 300);

    button_text(ok, "OK");
    button_text(cancel, "Cancel");
    button_OnClick(ok, listener(&form, i_OnAccountOk, Account));
    button_OnClick(cancel, listener(&form, i_OnAccountCancel, Account));

    layout_label(layout, name_label, 0, 0);
    layout_edit(layout, name, 1, 0);
    layout_label(layout, status_label, 0, 1);
    layout_edit(layout, status, 1, 1);
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

    if (form.accepted == TRUE)
        i_set_status(app, "Account updated");

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
    menuitem_text(account_item, "Account...");
    menuitem_OnClick(account_item, listener(app, i_OnAccount, App));
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
    menu_add_item(accounts_sub, status);
    menuitem_submenu(accounts, &accounts_sub);
    menu_add_item(menu, accounts);

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
    Layout *layout = layout_create(1, 4);
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
    app->account = label_create();
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
    i_set_status(app, "Ready");

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
    layout_label(layout, app->account, 0, 2);
    layout_label(layout, app->status, 0, 3);
    layout_vexpand(layout, 1);
    layout_vmargin(layout, 0, 8);
    layout_vmargin(layout, 2, 6);
    layout_vmargin(layout, 3, 6);
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

    while (cal_backend_poll(app->backend, &event) == TRUE)
    {
        i_rebuild_rows(app);
        i_refresh_table(app);
        if (event.buddy < CAL_MAX_CHATS && app->chats[event.buddy] != NULL)
            i_refresh_chat(app->chats[event.buddy]);
        cal_backend_store(app->backend);
        i_set_status(app, "Message received");
    }

    unref(prtime);
    unref(ctime);
}
