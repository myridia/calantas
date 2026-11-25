#include "gui.h"

#include <nappgui.h>
#include <osapp/osmain.h>

osmain_sync(.25, calantas_create, calantas_destroy, calantas_update, "", App)
