// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Also thanks to gcbenison, https://stackoverflow.com/a/9032052; and sardemff7, https://github.com/sardemff7/libgwater/blob/master/xcb/libgwater-xcb.c

#include "x11_event_source_glib.h"

struct _GLibX11Source {
  GSource source;
  Display *display;
  GPollFD poll_fd;
};

static gboolean XSourcePrepare(GSource *source, gint *timeout_ms) {
  GLibX11Source *gxsource = (GLibX11Source*) source;
  if (XPending(gxsource->display))
    *timeout_ms = 0;
  else
    *timeout_ms = -1;
  return FALSE;
}

static gboolean XSourceCheck(GSource *source) {
  GLibX11Source *gxsource = (GLibX11Source*) source;
  return XPending(gxsource->display);
}

static gboolean XSourceDispatch(GSource* source,
                         GSourceFunc callback,
                         gpointer user_data) {
  GLibX11Source *gxsource = (GLibX11Source*) source;

  gboolean ret = G_SOURCE_CONTINUE;
  while (XPending(gxsource->display) && ret == G_SOURCE_CONTINUE) {
    XEvent xevent;
    XNextEvent(gxsource->display, &xevent);
    ret = ((X11EventSourceGlibCallback)callback)(&xevent, user_data);
  }

  return ret;
}

static GSourceFuncs XSourceFuncs = {
  XSourcePrepare,
  XSourceCheck,
  XSourceDispatch,
  NULL
};

GLibX11Source* X11EventSourceGlib(Display *display, X11EventSourceGlibCallback callback, gpointer user_data, GDestroyNotify destroy_func) {
  int fd;
  g_return_val_if_fail(display != NULL, NULL);
  g_return_val_if_fail(callback != NULL, NULL);

  fd = ConnectionNumber(display);
  g_return_val_if_fail(fd > -1, NULL);

  GLibX11Source *gxsource = (GLibX11Source*) g_source_new(&XSourceFuncs, sizeof(GLibX11Source));
  gxsource->display = display;
  gxsource->poll_fd.fd = fd;
  gxsource->poll_fd.events = G_IO_IN;
  gxsource->poll_fd.revents = 0;

  g_source_add_poll((GSource*) gxsource, &gxsource->poll_fd);
  g_source_set_can_recurse((GSource*) gxsource, TRUE);
  g_source_set_callback((GSource*) gxsource, (GSourceFunc) callback, user_data, destroy_func);
  g_source_attach((GSource*) gxsource, NULL);

  return gxsource;
}

void X11EventSourceGlib_destroy(GLibX11Source *gxsource) {
  g_return_if_fail(gxsource != NULL);

  g_source_destroy((GSource*) gxsource);
  g_source_unref((GSource*) gxsource);
}
