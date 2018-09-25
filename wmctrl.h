#ifndef __wmctrl_h__
#define __wmctrl_h__

#include <glib.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>

G_BEGIN_DECLS

Window* get_client_list (Display *disp, unsigned long *size);

G_END_DECLS

#endif
