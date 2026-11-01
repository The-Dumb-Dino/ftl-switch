// The Switch app (ftl.nro): what main_switch.c and plat_switch.c share. Paths,
// settings.ini, the .ipa import and the mod overlay are the Linux app's
// (platform/linux/linux_app.h), with the Switch's paths in settings.c.
#pragma once
#include <stdbool.h>

int switch_app_init(void);          // window (EGL), input, applet hooks; 0 on success
void switch_push_options(void);     // settings.ini -> ux options
void switch_show_error(const char *msg);  // the error applet, or the console before the window exists
void port_log_flush(void);
