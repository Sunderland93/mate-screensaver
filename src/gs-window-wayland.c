/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2012-2026 MATE Developers
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */

#include "config.h"

#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <string.h>

#include <gtk/gtk.h>

#ifdef ENABLE_WAYLAND
#include <gdk/gdkwayland.h>
#include <wayland-client.h>
#include <libwlembed-gtk3/libwlembed-gtk3.h>
#include "ext-session-lock-client.h"
#endif

#include "gs-window.h"
#include "gs-window-private.h"
#include "gs-manager.h"
#include "gs-debug.h"

#ifdef ENABLE_WAYLAND

WleEmbeddedCompositor *gs_wayland_compositor = NULL;
struct ext_session_lock_v1 *gs_wayland_session_lock = NULL;

#define MATE_SCREENSAVER_DIALOG_PATH LIBEXECDIR "/mate-screensaver-dialog"

typedef struct GSWindowWaylandPrivate GSWindowWaylandPrivate;

struct GSWindowWaylandPrivate
{
	struct ext_session_lock_surface_v1 *lock_surface;

	GPid                                lock_pid;
	gint                                lock_watch_id;
	gint                                dialog_response;
	gboolean                            dialog_quit_requested;

	guint                               watchdog_timer_id;
	guint                               popup_dialog_idle_id;
	GTimer                             *timer;

	GtkWidget                          *vbox;
	GtkWidget                          *drawing_area;
};

typedef struct
{
	GSWindow parent;
} GSWindowWayland;

typedef struct
{
	GSWindowClass parent_class;
} GSWindowWaylandClass;

G_DEFINE_TYPE_WITH_PRIVATE (GSWindowWayland, gs_window_wayland, GS_TYPE_WINDOW)

#define GS_WINDOW_WAYLAND(o)      (G_TYPE_CHECK_INSTANCE_CAST ((o), GS_TYPE_WINDOW_WAYLAND, GSWindowWayland))
#define GS_IS_WINDOW_WAYLAND(o)   (G_TYPE_CHECK_INSTANCE_TYPE ((o), GS_TYPE_WINDOW_WAYLAND))
#define GS_TYPE_WINDOW_WAYLAND    (gs_window_wayland_get_type ())
#define GS_WINDOW_WAYLAND_GET_PRIVATE(o) ((GSWindowWaylandPrivate *)gs_window_wayland_get_instance_private (GS_WINDOW_WAYLAND (o)))

static void set_invisible_cursor (GdkWindow *window,
                                  gboolean   invisible);
static void gs_window_wayland_finalize (GObject *object);

static void popdown_dialog (GSWindow *window);

enum
{
	DIALOG_RESPONSE_CANCEL,
	DIALOG_RESPONSE_OK
};

static void
set_invisible_cursor (GdkWindow *window,
                      gboolean   invisible)
{
	GdkDisplay *display;
	GdkCursor  *cursor = NULL;

	if (invisible)
	{
		display = gdk_window_get_display (window);
		cursor = gdk_cursor_new_for_display (display, GDK_BLANK_CURSOR);
	}

	gdk_window_set_cursor (window, cursor);

	if (cursor)
	{
		g_object_unref (cursor);
	}
}

static void
wayland_window_set_dialog_up (GSWindow *window,
                              gboolean  dialog_up)
{
	if (window->priv->dialog_up == dialog_up)
	{
		return;
	}

	window->priv->dialog_up = (dialog_up != FALSE);
	g_object_notify (G_OBJECT (window), "dialog-up");
}

static gboolean
emit_deactivated_idle (gpointer data)
{
	GSWindow *window = GS_WINDOW (data);

	g_signal_emit (window, gs_window_signals [GS_WINDOW_SIGNAL_DEACTIVATED], 0);

	return FALSE;
}

static void
add_emit_deactivated_idle (GSWindow *window)
{
	g_idle_add (emit_deactivated_idle, window);
}

static void
remove_command_watches (GSWindow *window)
{
	GSWindowWaylandPrivate *priv;

	priv = GS_WINDOW_WAYLAND_GET_PRIVATE (window);

	if (priv->lock_watch_id != 0)
	{
		g_source_remove (priv->lock_watch_id);
		priv->lock_watch_id = 0;
	}
}

static void
gs_window_dialog_finish (GSWindow *window)
{
	GSWindowWaylandPrivate *priv;

	priv = GS_WINDOW_WAYLAND_GET_PRIVATE (window);

	if (priv->lock_pid != 0)
	{
		g_spawn_close_pid (priv->lock_pid);
		priv->lock_pid = 0;
	}

	remove_command_watches (window);
}

static void
maybe_kill_dialog (GSWindow *window)
{
	GSWindowWaylandPrivate *priv;

	priv = GS_WINDOW_WAYLAND_GET_PRIVATE (window);

	if (priv->lock_pid != 0 && !priv->dialog_quit_requested)
	{
		gs_debug ("Sending TERM to dialog process %d", priv->lock_pid);
		kill (priv->lock_pid, SIGTERM);
	}
}

static gboolean
lock_command_watch (GIOChannel   *source,
                    GIOCondition  condition,
                    GSWindow     *window)
{
	GSWindowWaylandPrivate *priv;
	gboolean                finished = FALSE;

	g_return_val_if_fail (GS_IS_WINDOW (window), FALSE);

	priv = GS_WINDOW_WAYLAND_GET_PRIVATE (window);

	if (condition & G_IO_IN)
	{
		GIOStatus status;
		GError   *error = NULL;
		char     *line;

		line = NULL;
		status = g_io_channel_read_line (source, &line, NULL, NULL, &error);

		switch (status)
		{
		case G_IO_STATUS_NORMAL:
			gs_debug ("command output: %s", line);

			if (strstr (line, "RESPONSE=") != NULL)
			{
				if (strstr (line, "RESPONSE=OK") != NULL)
				{
					gs_debug ("Got OK response");
					priv->dialog_response = DIALOG_RESPONSE_OK;
				}
				else
				{
					gs_debug ("Got CANCEL response");
					priv->dialog_response = DIALOG_RESPONSE_CANCEL;
				}
				finished = TRUE;
			}
			else if (strstr (line, "NOTICE=") != NULL)
			{
				if (strstr (line, "NOTICE=AUTH FAILED") != NULL)
				{
					gs_debug ("Auth failed");
				}
			}
			else if (strstr (line, "REQUEST QUIT") != NULL)
			{
				gs_debug ("Dialog requested quit");
				priv->dialog_quit_requested = TRUE;
				maybe_kill_dialog (window);
			}
			break;
		case G_IO_STATUS_EOF:
			gs_debug ("Got EOF from dialog");
			finished = TRUE;
			break;
		default:
			break;
		}

		g_free (line);
	}
	else if (condition & (G_IO_HUP | G_IO_ERR))
	{
		gs_debug ("Got HUP/ERR from dialog");
		finished = TRUE;
	}

	if (finished)
	{
		gint response;

		response = priv->dialog_response;

		popdown_dialog (window);

		if (response == DIALOG_RESPONSE_OK)
		{
			add_emit_deactivated_idle (window);
		}

		return FALSE;
	}

	return TRUE;
}

static void
popdown_dialog (GSWindow *window)
{
	GSWindowWaylandPrivate *priv;

	priv = GS_WINDOW_WAYLAND_GET_PRIVATE (window);

	gs_window_dialog_finish (window);

	gtk_widget_show (priv->drawing_area);

	set_invisible_cursor (gtk_widget_get_window (GTK_WIDGET (window)), TRUE);

	priv->dialog_quit_requested = FALSE;

	if (priv->popup_dialog_idle_id != 0)
	{
		g_source_remove (priv->popup_dialog_idle_id);
		priv->popup_dialog_idle_id = 0;
	}

	wayland_window_set_dialog_up (window, FALSE);
}

static gboolean
spawn_on_window (GSWindow     *window,
                 const char   *command,
                 GPid         *child_pid,
                 GIOFunc       watch_func,
                 gpointer      watch_data,
                 gint         *watch_id)
{
	gboolean   result;
	GError    *error;
	char     **argv;
	char     **envp;
	GIOChannel *channel;

	g_return_val_if_fail (GS_IS_WINDOW (window), FALSE);
	g_return_val_if_fail (command != NULL, FALSE);
	g_return_val_if_fail (child_pid != NULL, FALSE);

	error = NULL;

	if (! g_shell_parse_argv (command, NULL, &argv, &error))
	{
		gs_debug ("Could not parse command: %s", error->message);
		g_error_free (error);
		return FALSE;
	}

	envp = g_get_environ ();

	result = g_spawn_async_with_pipes (NULL,
	                                   argv,
	                                   envp,
	                                   G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_SEARCH_PATH,
	                                   NULL,
	                                   NULL,
	                                   child_pid,
	                                   NULL,
	                                   NULL,
	                                   NULL,
	                                   &error);

	g_strfreev (argv);
	g_strfreev (envp);

	if (! result)
	{
		gs_debug ("Could not start command: %s", error->message);
		g_error_free (error);
		return FALSE;
	}

	channel = g_io_channel_unix_new (*child_pid);
	g_io_channel_set_close_on_unref (channel, TRUE);
	g_io_channel_set_flags (channel, G_IO_FLAG_NONBLOCK, NULL);
	*watch_id = g_io_add_watch (channel,
	                            G_IO_IN | G_IO_HUP | G_IO_ERR,
	                            watch_func,
	                            watch_data);
	g_io_channel_unref (channel);

	return TRUE;
}

static void
popup_dialog (GSWindow *window)
{
	GSWindowWaylandPrivate *priv;
	WleEmbeddedCompositor  *compositor;
	gboolean                result;
	GString                *command;

	priv = GS_WINDOW_WAYLAND_GET_PRIVATE (window);

	gs_debug ("Popping up dialog");

	if (! gtk_window_is_active (GTK_WINDOW (window)))
	{
		gs_debug ("Window is not active, deferring dialog");
		return;
	}

	compositor = gs_wayland_compositor;
	if (compositor == NULL)
	{
		gs_debug ("No embedded compositor available");
		return;
	}

	command = g_string_new (MATE_SCREENSAVER_DIALOG_PATH);

	if (window->priv->logout_enabled)
	{
		command = g_string_append (command, " --enable-logout");
		g_string_append_printf (command, " --logout-command='%s'", window->priv->logout_command);
	}

	if (window->priv->status_message)
	{
		char *quoted;

		quoted = g_shell_quote (window->priv->status_message);
		g_string_append_printf (command, " --status-message=%s", quoted);
		g_free (quoted);
	}

	if (window->priv->user_switch_enabled)
	{
		command = g_string_append (command, " --enable-switch");
	}

	if (gs_debug_enabled ())
	{
		command = g_string_append (command, " --verbose");
	}

	gtk_widget_hide (priv->drawing_area);

	gtk_widget_queue_draw (GTK_WIDGET (window));
	set_invisible_cursor (gtk_widget_get_window (GTK_WIDGET (window)), FALSE);

	priv->dialog_quit_requested = FALSE;
	priv->dialog_response = DIALOG_RESPONSE_CANCEL;

	result = spawn_on_window (window,
	                          command->str,
	                          &priv->lock_pid,
	                          (GIOFunc) lock_command_watch,
	                          window,
	                          &priv->lock_watch_id);
	if (! result)
	{
		gs_debug ("Could not start command: %s", command->str);
	}

	g_string_free (command, TRUE);
}

static gboolean
popup_dialog_idle (gpointer data)
{
	GSWindow *window = data;

	popup_dialog (window);

	GS_WINDOW_WAYLAND_GET_PRIVATE (window)->popup_dialog_idle_id = 0;

	return FALSE;
}

static void
add_popup_dialog_idle (GSWindow *window)
{
	GSWindowWaylandPrivate *priv;

	priv = GS_WINDOW_WAYLAND_GET_PRIVATE (window);

	if (priv->popup_dialog_idle_id != 0)
	{
		return;
	}

	priv->popup_dialog_idle_id = g_idle_add (popup_dialog_idle, window);
}

static void
destroy_lock_surface (GSWindow *window)
{
	GSWindowWaylandPrivate *priv;

	priv = GS_WINDOW_WAYLAND_GET_PRIVATE (window);

	if (priv->lock_surface != NULL)
	{
		ext_session_lock_surface_v1_destroy (priv->lock_surface);
		priv->lock_surface = NULL;
	}
}

static gboolean
watchdog_timer_cb (gpointer data)
{
	GSWindow  *window = GS_WINDOW (data);
	GtkWidget *widget = GTK_WIDGET (window);
	GdkWindow *gdkwindow;

	gdkwindow = gtk_widget_get_window (widget);
	if (gdkwindow != NULL)
	{
		gdk_window_focus (gdkwindow, GDK_CURRENT_TIME);
	}

	return G_SOURCE_CONTINUE;
}

static void
remove_watchdog_timer (GSWindow *window)
{
	GSWindowWaylandPrivate *priv;

	priv = GS_WINDOW_WAYLAND_GET_PRIVATE (window);

	if (priv->watchdog_timer_id != 0)
	{
		g_source_remove (priv->watchdog_timer_id);
		priv->watchdog_timer_id = 0;
	}
}

static void
add_watchdog_timer (GSWindow *window,
                    guint     timeout)
{
	GSWindowWaylandPrivate *priv;

	priv = GS_WINDOW_WAYLAND_GET_PRIVATE (window);

	priv->watchdog_timer_id = g_timeout_add (timeout,
	                                        watchdog_timer_cb,
	                                        window);
}

static void
lock_surface_handle_configure (void                               *data,
                               struct ext_session_lock_surface_v1 *surface,
                               uint32_t                            serial,
                               uint32_t                            width,
                               uint32_t                            height)
{
	GSWindow *window = GS_WINDOW (data);

	ext_session_lock_surface_v1_ack_configure (surface, serial);

	gdk_window_move_resize (gtk_widget_get_window (GTK_WIDGET (window)),
	                        0, 0, width, height);

	gs_debug ("Lock surface configured: %ux%u", width, height);
}

static const struct ext_session_lock_surface_v1_listener lock_surface_listener =
{
	lock_surface_handle_configure,
};

void
gs_window_wayland_create_lock_surface (GSWindow *window)
{
	GSWindowWaylandPrivate    *priv;
	GdkWindow                 *gdk_window;
	GdkMonitor                *monitor;
	struct wl_surface         *wl_surface;
	struct wl_output          *wl_output;
	struct ext_session_lock_v1 *session_lock;

	g_return_if_fail (GS_IS_WINDOW_WAYLAND (window));

	priv = GS_WINDOW_WAYLAND_GET_PRIVATE (window);

	if (priv->lock_surface != NULL)
	{
		gs_debug ("Lock surface already exists");
		return;
	}

	session_lock = gs_wayland_session_lock;
	if (session_lock == NULL)
	{
		gs_debug ("No session lock available from manager");
		return;
	}

	gdk_window = gtk_widget_get_window (GTK_WIDGET (window));
	if (gdk_window == NULL)
	{
		gs_debug ("No GDK window available");
		return;
	}

	gdk_wayland_window_set_use_custom_surface (gdk_window);

	wl_surface = gdk_wayland_window_get_wl_surface (gdk_window);
	if (wl_surface == NULL)
	{
		gs_debug ("Failed to get Wayland surface");
		return;
	}

	monitor = gs_window_get_monitor (window);
	if (monitor != NULL)
	{
		wl_output = gdk_wayland_monitor_get_wl_output (monitor);
	}
	else
	{
		wl_output = NULL;
	}

	priv->lock_surface = ext_session_lock_v1_get_lock_surface (session_lock,
	                                                           wl_surface,
	                                                           wl_output);
	if (priv->lock_surface != NULL)
	{
		ext_session_lock_surface_v1_add_listener (priv->lock_surface,
		                                          &lock_surface_listener,
		                                          window);
		wl_display_roundtrip (gdk_wayland_display_get_wl_display (gdk_display_get_default ()));
		gs_debug ("Lock surface created for output");
	}
	else
	{
		gs_debug ("Failed to create lock surface");
	}
}

static void
gs_window_wayland_request_unlock (GSWindow *window)
{
	GSWindowWaylandPrivate *priv;

	g_return_if_fail (GS_IS_WINDOW_WAYLAND (window));

	priv = GS_WINDOW_WAYLAND_GET_PRIVATE (window);

	if (! gtk_widget_get_mapped (GTK_WIDGET (window)))
	{
		return;
	}

	if (priv->lock_watch_id != 0)
	{
		return;
	}

	if (! window->priv->lock_enabled)
	{
		add_emit_deactivated_idle (window);
		return;
	}

	gs_debug ("Wayland request unlock");

	add_popup_dialog_idle (window);

	wayland_window_set_dialog_up (window, TRUE);
}

static void
gs_window_wayland_cancel_unlock_request (GSWindow *window)
{
	GSWindowWaylandPrivate *priv;

	g_return_if_fail (GS_IS_WINDOW_WAYLAND (window));

	priv = GS_WINDOW_WAYLAND_GET_PRIVATE (window);

	destroy_lock_surface (window);

	wayland_window_set_dialog_up (window, FALSE);
}

static gboolean
on_drawing_area_draw (GtkWidget *widget,
                      cairo_t   *cr)
{
	cairo_set_operator (cr, CAIRO_OPERATOR_OVER);
	cairo_set_source_rgb (cr, 0, 0, 0);
	cairo_paint (cr);

	return FALSE;
}

static void
gs_window_real_show (GSWindow *window)
{
	GtkWidget              *widget = GTK_WIDGET (window);
	GSWindowWaylandPrivate *priv;

	if (GTK_WIDGET_CLASS (gs_window_wayland_parent_class)->show)
	{
		GTK_WIDGET_CLASS (gs_window_wayland_parent_class)->show (widget);
	}

	gs_window_clear (window);

	set_invisible_cursor (gtk_widget_get_window (widget), TRUE);

	priv = GS_WINDOW_WAYLAND_GET_PRIVATE (window);

	if (priv->timer != NULL)
	{
		g_timer_destroy (priv->timer);
	}
	priv->timer = g_timer_new ();

	remove_watchdog_timer (window);
	add_watchdog_timer (window, 30000);
}

static void
gs_window_real_hide (GtkWidget *widget)
{
	GSWindow                *window = GS_WINDOW (widget);
	GSWindowWaylandPrivate  *priv;

	priv = GS_WINDOW_WAYLAND_GET_PRIVATE (window);

	remove_watchdog_timer (window);

	popdown_dialog (window);

	destroy_lock_surface (window);

	if (GTK_WIDGET_CLASS (gs_window_wayland_parent_class)->hide)
	{
		GTK_WIDGET_CLASS (gs_window_wayland_parent_class)->hide (widget);
	}
}

static gboolean
gs_window_real_draw (GtkWidget *widget,
                     cairo_t   *cr)
{
	cairo_set_operator (cr, CAIRO_OPERATOR_OVER);
	cairo_set_source_rgb (cr, 0, 0, 0);
	cairo_paint (cr);

	return FALSE;
}

static void
gs_window_real_realize (GtkWidget *widget)
{
	if (GTK_WIDGET_CLASS (gs_window_wayland_parent_class)->realize)
	{
		GTK_WIDGET_CLASS (gs_window_wayland_parent_class)->realize (widget);
	}

	gtk_widget_set_app_paintable (widget, TRUE);

	gs_debug ("Wayland window realized");
}

static void
gs_window_real_unrealize (GtkWidget *widget)
{
	GSWindowWaylandPrivate *priv;

	priv = GS_WINDOW_WAYLAND_GET_PRIVATE (widget);

	remove_watchdog_timer (GS_WINDOW (widget));

	destroy_lock_surface (GS_WINDOW (widget));

	if (GTK_WIDGET_CLASS (gs_window_wayland_parent_class)->unrealize)
	{
		GTK_WIDGET_CLASS (gs_window_wayland_parent_class)->unrealize (widget);
	}
}

static gboolean
gs_window_real_key_press_event (GtkWidget   *widget,
                                GdkEventKey *event)
{
	GSWindow *window = GS_WINDOW (widget);

	g_signal_emit (window, gs_window_signals [GS_WINDOW_SIGNAL_ACTIVITY], 0);

	if (GTK_WIDGET_CLASS (gs_window_wayland_parent_class)->key_press_event)
	{
		GTK_WIDGET_CLASS (gs_window_wayland_parent_class)->key_press_event (widget, event);
	}

	return TRUE;
}

static gboolean
gs_window_real_button_press_event (GtkWidget      *widget,
                                   GdkEventButton *event)
{
	GSWindow *window = GS_WINDOW (widget);

	g_signal_emit (window, gs_window_signals [GS_WINDOW_SIGNAL_ACTIVITY], 0);

	return FALSE;
}

static gboolean
gs_window_real_scroll_event (GtkWidget      *widget,
                             GdkEventScroll *event)
{
	GSWindow *window = GS_WINDOW (widget);

	g_signal_emit (window, gs_window_signals [GS_WINDOW_SIGNAL_ACTIVITY], 0);

	return FALSE;
}

static gboolean
gs_window_real_motion_notify_event (GtkWidget      *widget,
                                    GdkEventMotion *event)
{
	GSWindow *window = GS_WINDOW (widget);

	g_signal_emit (window, gs_window_signals [GS_WINDOW_SIGNAL_ACTIVITY], 0);

	return FALSE;
}

static void
gs_window_wayland_class_init (GSWindowWaylandClass *klass)
{
	GObjectClass   *object_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
	GSWindowClass  *window_class = GS_WINDOW_CLASS (klass);

	object_class->finalize = gs_window_wayland_finalize;

	window_class->real_show = gs_window_real_show;
	window_class->real_destroy = NULL;
	window_class->request_unlock = gs_window_wayland_request_unlock;
	window_class->cancel_unlock_request = gs_window_wayland_cancel_unlock_request;
	window_class->create_lock_surface = gs_window_wayland_create_lock_surface;

	widget_class->hide                = gs_window_real_hide;
	widget_class->draw                = gs_window_real_draw;
	widget_class->realize             = gs_window_real_realize;
	widget_class->unrealize           = gs_window_real_unrealize;
	widget_class->key_press_event     = gs_window_real_key_press_event;
	widget_class->button_press_event  = gs_window_real_button_press_event;
	widget_class->scroll_event        = gs_window_real_scroll_event;
	widget_class->motion_notify_event = gs_window_real_motion_notify_event;
}

static void
gs_window_wayland_init (GSWindowWayland *wayland)
{
	GSWindowWaylandPrivate *priv;

	priv = GS_WINDOW_WAYLAND_GET_PRIVATE (wayland);

	priv->lock_surface = NULL;
	priv->lock_pid = 0;
	priv->lock_watch_id = 0;
	priv->dialog_response = DIALOG_RESPONSE_CANCEL;
	priv->dialog_quit_requested = FALSE;
	priv->watchdog_timer_id = 0;
	priv->popup_dialog_idle_id = 0;
	priv->timer = NULL;
	priv->vbox = NULL;
	priv->drawing_area = NULL;

	gtk_window_set_decorated (GTK_WINDOW (wayland), FALSE);

	gtk_window_set_skip_taskbar_hint (GTK_WINDOW (wayland), TRUE);
	gtk_window_set_skip_pager_hint (GTK_WINDOW (wayland), TRUE);

	gtk_window_set_keep_above (GTK_WINDOW (wayland), TRUE);

	gtk_window_fullscreen (GTK_WINDOW (wayland));

	gtk_widget_set_events (GTK_WIDGET (wayland),
	                       gtk_widget_get_events (GTK_WIDGET (wayland))
	                       | GDK_POINTER_MOTION_MASK
	                       | GDK_BUTTON_PRESS_MASK
	                       | GDK_BUTTON_RELEASE_MASK
	                       | GDK_KEY_PRESS_MASK
	                       | GDK_KEY_RELEASE_MASK
	                       | GDK_EXPOSURE_MASK
	                       | GDK_ENTER_NOTIFY_MASK
	                       | GDK_LEAVE_NOTIFY_MASK);

	priv->vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
	gtk_widget_show (priv->vbox);
	gtk_container_add (GTK_CONTAINER (wayland), priv->vbox);

	priv->drawing_area = gtk_drawing_area_new ();
	gtk_widget_show (priv->drawing_area);
	gtk_widget_set_app_paintable (priv->drawing_area, TRUE);
	gtk_box_pack_start (GTK_BOX (priv->vbox),
	                    priv->drawing_area, TRUE, TRUE, 0);
	g_signal_connect (priv->drawing_area,
	                  "draw",
	                  G_CALLBACK (on_drawing_area_draw),
	                  NULL);
}

static void
gs_window_wayland_finalize (GObject *object)
{
	GSWindow                *window;
	GSWindowWaylandPrivate  *priv;

	g_return_if_fail (object != NULL);
	g_return_if_fail (GS_IS_WINDOW (object));

	window = GS_WINDOW (object);

	g_return_if_fail (window->priv != NULL);

	priv = GS_WINDOW_WAYLAND_GET_PRIVATE (window);

	remove_watchdog_timer (window);

	if (priv->timer != NULL)
	{
		g_timer_destroy (priv->timer);
		priv->timer = NULL;
	}

	popdown_dialog (window);

	destroy_lock_surface (window);

	/* Note: common fields (logout_command, keyboard_command, status_message)
	 * are freed by the base class finalize */

	G_OBJECT_CLASS (gs_window_wayland_parent_class)->finalize (object);
}

GSWindow *
gs_window_wayland_new (GdkMonitor *monitor,
                      gboolean   lock_enabled)
{
	GObject *result;

	result = g_object_new (GS_TYPE_WINDOW_WAYLAND,
	                       "type", GTK_WINDOW_POPUP,
	                       "monitor", monitor,
	                       "lock-enabled", lock_enabled,
	                       "app-paintable", TRUE,
	                       NULL);

	return GS_WINDOW (result);
}

#endif /* ENABLE_WAYLAND */
