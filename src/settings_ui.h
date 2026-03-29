#ifndef SETTINGS_UI_H
#define SETTINGS_UI_H

#include "config.h"

/* Show the settings dialog. on_save is called when the user clicks 'Save'. */
void settings_ui_show(Config *cfg, void (*on_save)(void));

#endif // SETTINGS_UI_H
