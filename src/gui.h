#ifndef CALANTAS_GUI_H
#define CALANTAS_GUI_H

#include <core/core.hxx>

typedef struct _app_t App;

App *calantas_create(void);
void calantas_destroy(App **app);
void calantas_update(App *app, const real64_t prtime, const real64_t ctime);

#endif
