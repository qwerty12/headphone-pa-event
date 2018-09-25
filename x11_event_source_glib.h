#ifndef __x11_event_source_glib_h__
#define __x11_event_source_glib_h__

#include <glib.h>
#include <X11/Xlib.h>

G_BEGIN_DECLS

typedef struct _GLibX11Source GLibX11Source;

typedef gboolean (*X11EventSourceGlibCallback)(const XEvent *event, gpointer user_data);

GLibX11Source* X11EventSourceGlib(Display *display, X11EventSourceGlibCallback callback, gpointer user_data, GDestroyNotify destroy_func);

void X11EventSourceGlib_destroy(GLibX11Source *gxsource);

G_END_DECLS

#endif
