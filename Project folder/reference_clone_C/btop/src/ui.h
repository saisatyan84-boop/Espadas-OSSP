#ifndef UI_H
#define UI_H

#include "wintop.h"

void ui_init(void);
void ui_render(void);
void ui_shutdown(void);
void ui_handle_resize(void);
void ui_render_once(void);

#endif // UI_H
