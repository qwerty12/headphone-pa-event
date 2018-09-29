// cc -Wall -O2 -s `pkg-config --cflags --libs gio-unix-2.0 libpulse-mainloop-glib x11` main.c x11_event_source_glib.c wmctrl.c -o headphone-pa-event

/*
	Parts based on projedi's (Alexander Shabalin) https://github.com/projedi/headphone-event
	and https://www.tedunangst.com/flak/post/logging-the-foreground-process-in-X11
*/

#include <glib/gstdio.h>

#include <glib-unix.h>
#include <gio/gio.h>

#include <pulse/pulseaudio.h>
#include <pulse/glib-mainloop.h>

#include "x11_event_source_glib.h"
#include "wmctrl.h"

static GMainLoop *loop = NULL;
static gboolean pulse_ready = FALSE;
static pa_glib_mainloop *pa_loop = NULL;
static pa_context *pa_ctx = NULL;

static Display *disp = NULL;
static GLibX11Source *gxsource = NULL;
static GPtrArray *number_list = NULL;

static GDBusConnection *sess_conn = NULL;

static uint32_t card_idx = 0;

static gboolean headphones = FALSE;

#define fail_log(context, name) {\
	g_critical(name " failed: %s", pa_strerror(pa_context_errno(context)));\
	exit(1);\
}

static gboolean pulseeffects_running()
{
	gboolean ret = FALSE;

	if (sess_conn) {
		g_autoptr(GVariant) res = g_dbus_connection_call_sync(sess_conn, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "NameHasOwner", g_variant_new("(s)", "com.github.wwmm.pulseeffects"), NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL);

		if (res)
			g_variant_get(res, "(b)", &ret);
	}

	return ret;
}

static void deinit_pulse()
{
	pulse_ready = FALSE;

	if (pa_ctx) {
		pa_context_disconnect(pa_ctx);
		g_clear_pointer(&pa_ctx, pa_context_unref);
	}

	pulse_ready = FALSE;

	g_clear_pointer(&pa_loop, pa_glib_mainloop_free);
}

static gboolean on_sigint(gpointer data_ptr G_GNUC_UNUSED)
{
	g_main_loop_quit(loop);
	return G_SOURCE_REMOVE;
}

static void cleanup()
{
	g_clear_pointer(&gxsource, X11EventSourceGlib_destroy);
	g_clear_pointer(&disp, XCloseDisplay);
	deinit_pulse();
	if (number_list) {
		g_ptr_array_free(number_list, TRUE);
		number_list = NULL;
	}
	g_clear_object(&sess_conn);
	g_clear_pointer(&loop, g_main_loop_unref);
}

static void child_setup(gpointer user_data G_GNUC_UNUSED) { setsid(); }

static void pa_sink_info_set_vol_cb(pa_context* context, const pa_sink_info* i, int eol, void* userdata G_GNUC_UNUSED)
{
	static const pa_volume_t newvol = PA_VOLUME_NORM * 40 / 100;
	pa_cvolume vol;

	if (eol)
		return;

	if (newvol > pa_cvolume_avg(&i->volume))
		return;

	pa_cvolume_init(&vol);
	pa_cvolume_set(&vol, i->volume.channels, newvol);
	pa_operation_unref(pa_context_set_sink_volume_by_index(context, i->index, &vol, NULL, NULL));
}

static void pa_server_info_callback(pa_context *context, const pa_server_info *i, void *userdata G_GNUC_UNUSED)
{
    if (i->default_sink_name)
		pa_operation_unref(pa_context_get_sink_info_by_name(context, i->default_sink_name, pa_sink_info_set_vol_cb, NULL));
}

static int xerrhandler(Display *d G_GNUC_UNUSED, XErrorEvent *e G_GNUC_UNUSED) { return 0; }

static int getWindowName(Display *disp, Window w, char *buf, size_t buflen)
{
	XTextProperty prop;
	return XGetTextProperty(disp, w, &prop, XA_WM_NAME) ? snprintf(buf, buflen, "%s", prop.value) : 0;
}

static gboolean is_mpv_main_window(Display *disp, const Window w)
{
	char name[256];

	if (!w || !disp)
		return FALSE;

	return getWindowName(disp, w, name, sizeof(name)) && g_str_has_suffix(name, " - mpv");
}

static gboolean x11_event_cb(const XEvent *event, gpointer user_data G_GNUC_UNUSED)
{
	// I realised WNCK existed far too late in the process

	switch (event->type) {
		case MapNotify:
		{
			XMapEvent *mev = (XMapEvent *)event;
			if (is_mpv_main_window(disp, mev->window)) {
				g_ptr_array_add(number_list, GINT_TO_POINTER(mev->window));
				if (pulse_ready && headphones && number_list->len == 1)
					pa_operation_unref(pa_context_get_server_info(pa_ctx, pa_server_info_callback, NULL));
			}
			break;
		}
		case UnmapNotify:
		{
			XUnmapEvent *uev = (XUnmapEvent *)event;
			if (number_list->len)
				g_ptr_array_remove_fast(number_list, GINT_TO_POINTER(uev->window));
			break;
		}
		default:
			break;
	}

	return G_SOURCE_CONTINUE;
}

static void pa_sink_info_by_card_cb(pa_context *ctx, const pa_sink_info *i, int eol, void *userdata G_GNUC_UNUSED)
{
    if (eol)
        return;

    if (i->card != card_idx)
        return;

    pa_operation_unref(pa_context_set_sink_mute_by_index(ctx, i->index, 1, NULL, NULL));
}

static void headphones_unplugged(pa_card_info const* card)
{
	headphones = FALSE;

	card_idx = card->index;
	pa_operation_unref(pa_context_get_sink_info_list(pa_ctx, pa_sink_info_by_card_cb, NULL));

	if (pulseeffects_running()) {
		gchar *argv[] = { "/usr/bin/pulseeffects", "-q", NULL };
		g_spawn_async(NULL, argv, NULL, G_SPAWN_DEFAULT, NULL, NULL, NULL, NULL);
	}
}

static void headphones_plugged(pa_card_info const* card G_GNUC_UNUSED)
{
	headphones = TRUE;
	if (!pulseeffects_running()) {
		gchar *argv[] = { "/usr/bin/pulseeffects", "--gapplication-service", "-l", "Boosted", NULL };
		g_autoptr(GError) error = NULL;

		g_spawn_async(NULL, argv, NULL, G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL, child_setup, NULL, NULL, &error);
		if (error)
		    g_error("Spawning child failed: %s", error->message);
	}

	if (number_list->len)
		pa_operation_unref(pa_context_get_server_info(pa_ctx, pa_server_info_callback, NULL));
}

static void pa_get_card_info_callback(pa_context *context, pa_card_info const* card, int is_last, void *userdata G_GNUC_UNUSED)
{
	if (is_last < 0) {
		g_warning("Failed to get card information: %s", pa_strerror(pa_context_errno(context)));
		return;
	}

	if (is_last)
		return;

	if (!card->ports)
		return;

	for (pa_card_port_info **p = card->ports; *p; ++p) {
		if (!g_strcmp0((*p)->name, "analog-output-headphones")) {
			switch((*p)->available) {
				case PA_PORT_AVAILABLE_YES:
					headphones_plugged(card);
					break;
				case PA_PORT_AVAILABLE_NO:
					headphones_unplugged(card);
					break;
				default:
					break;
			}
		}
	}
}

static void pa_context_subscribe_callback(pa_context *context, pa_subscription_event_type_t type, uint32_t card_idx, void *userdata G_GNUC_UNUSED)
{
	if ((type & PA_SUBSCRIPTION_EVENT_TYPE_MASK) == PA_SUBSCRIPTION_EVENT_CHANGE)
		pa_operation_unref(pa_context_get_card_info_by_index(context, card_idx, pa_get_card_info_callback, NULL));
}

static void pa_sink_info_get_headphone_current_state_cb(pa_context *ctx, const pa_sink_info *i, int eol, void *userdata G_GNUC_UNUSED)
{
	/* TODO: Look at default sink only. Maybe. Getting an enema from a bicycle pump is honestly more preferable to using the PA API. */
	if (eol)
		return;

	if (i->card != PA_INVALID_INDEX && i->active_port->available == PA_PORT_AVAILABLE_YES && !g_strcmp0(i->active_port->name, "analog-output-headphones"))
		headphones_plugged(NULL);
}

static void pa_context_state_callback(pa_context *context, void *userdata G_GNUC_UNUSED)
{
    if ((pulse_ready = pa_context_get_state(context) == PA_CONTEXT_READY)) {
		pa_operation_unref(pa_context_get_sink_info_list(context, pa_sink_info_get_headphone_current_state_cb, NULL));
		pa_context_set_subscribe_callback(context, pa_context_subscribe_callback, NULL);
		pa_operation_unref(pa_context_subscribe(context, PA_SUBSCRIPTION_MASK_CARD, NULL, NULL));
	}
}

static void init_pulse()
{
	if (!(pa_loop = pa_glib_mainloop_new(NULL)))
		fail_log(NULL, "pa_glib_mainloop_new");

	if ((pa_ctx = pa_context_new(pa_glib_mainloop_get_api(pa_loop), "headphone-pa-event"))) {
		pa_context_set_state_callback(pa_ctx, pa_context_state_callback, NULL);
		pa_context_connect(pa_ctx, NULL, PA_CONTEXT_NOAUTOSPAWN | PA_CONTEXT_NOFAIL, NULL);
	} else {
		fail_log(NULL, "pa_context_new");
	}
}

static void init_x11()
{
	if ((disp = XOpenDisplay(NULL))) {
		unsigned long nwin;

		XSetErrorHandler(xerrhandler);

		Window *win = get_client_list(disp, &nwin);
		if (win) {
			for (unsigned long i = 0; i < nwin / sizeof(Window); ++i) {
				if (is_mpv_main_window(disp, win[i]))
					g_ptr_array_add(number_list, GINT_TO_POINTER(win[i]));
			}
			g_free(win);
		}

		XSelectInput(disp, RootWindow(disp, DefaultScreen(disp)), SubstructureNotifyMask);
		gxsource = X11EventSourceGlib(disp, x11_event_cb, NULL, NULL);
	}
}

int main()
{
	loop = g_main_loop_new(NULL, FALSE);
	g_unix_signal_add(SIGINT, on_sigint, NULL);
	g_unix_signal_add(SIGTERM, on_sigint, NULL);

	number_list = g_ptr_array_sized_new(2);
	sess_conn = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, NULL);

	init_x11();
	init_pulse();

	g_main_loop_run(loop);

	cleanup();
	return EXIT_SUCCESS;
}
