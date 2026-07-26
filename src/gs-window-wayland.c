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

#include <gtk/gtk.h>

#ifdef ENABLE_WAYLAND
#include <gdk/gdkwayland.h>
#include <wayland-client.h>
#include "ext-idle-notify-client.h"
#include "ext-session-lock-client.h"
#endif

#include "gs-window.h"
#include "gs-window-private.h"
#include "gs-debug.h"

#ifdef ENABLE_WAYLAND

#define MATE_SCREENSAVER_DIALOG_PATH LIBEXECDIR "/mate-screensaver-dialog"

typedef struct GSWindowWaylandPrivate GSWindowWaylandPrivate;

struct GSWindowWaylandPrivate
{
	struct wl_surface                  *wl_surface;
	struct ext_session_lock_v1         *session_lock;
	struct ext_session_lock_surface_v1 *lock_surface;
	gboolean                            is_locked;
	gboolean                            locked_confirmed;

	struct ext_idle_notification_v1    *idle_notification;
	guint                               idle_timeout_id;

	guint                               watchdog_timer_id;
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
session_lock_handle_locked (void                        *data,
                            struct ext_session_lock_v1 *session_lock)
{
	GSWindow             *window = GS_WINDOW (data);
	GSWindowWaylandPrivate *priv;

	priv = GS_WINDOW_WAYLAND_GET_PRIVATE (window);

	priv->locked_confirmed = TRUE;

	gs_debug ("Session lock confirmed by compositor");
}

static void
session_lock_handle_finished (void                        *data,
                              struct ext_session_lock_v1 *session_lock)
{
	GSWindow             *window = GS_WINDOW (data);
	GSWindowWaylandPrivate *priv;

	priv = GS_WINDOW_WAYLAND_GET_PRIVATE (window);

	gs_debug ("Session lock finished by compositor");

	priv->is_locked = FALSE;
	priv->locked_confirmed = FALSE;

	if (priv->lock_surface != NULL)
	{
		ext_session_lock_surface_v1_destroy (priv->lock_surface);
		priv->lock_surface = NULL;
	}
}

static const struct ext_session_lock_v1_listener session_lock_listener =
{
	session_lock_handle_locked,
	session_lock_handle_finished,
};

static void
lock_surface_handle_configure (void                               *data,
                               struct ext_session_lock_surface_v1 *surface,
                               uint32_t                            serial,
                               uint32_t                            width,
                               uint32_t                            height)
{
	GSWindow *window = GS_WINDOW (data);

	ext_session_lock_surface_v1_ack_configure (surface, serial);

	gtk_widget_set_size_request (GTK_WIDGET (window), width, height);

	gs_debug ("Lock surface configured: %ux%u", width, height);
}

static const struct ext_session_lock_surface_v1_listener lock_surface_listener =
{
	lock_surface_handle_configure,
};

static void
wayland_window_set_session_lock (GSWindow *window)
{
	GSWindowWaylandPrivate *priv;
	GdkWindow              *gdk_window;

	priv = GS_WINDOW_WAYLAND_GET_PRIVATE (window);

	gdk_window = gtk_widget_get_window (GTK_WIDGET (window));
	if (gdk_window == NULL)
	{
		gs_debug ("No GDK window available for session lock");
		return;
	}

	priv->wl_surface = gdk_wayland_window_get_wl_surface (gdk_window);
	if (priv->wl_surface == NULL)
	{
		gs_debug ("Failed to get Wayland surface");
		return;
	}

	if (priv->session_lock != NULL)
	{
		gs_debug ("Session lock already created");
		return;
	}

	priv->is_locked = TRUE;
	priv->locked_confirmed = FALSE;

	/* The session lock is expected to have been obtained from the
	 * compositor registry by the manager. For now we mark the
	 * pending state; the actual ext_session_lock_v1 object will be
	 * set when the manager provides it via
	 * gs_window_wayland_set_session_lock(). */
	gs_debug ("Session lock requested");
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

	wayland_window_set_session_lock (window);
}

static void
gs_window_real_hide (GtkWidget *widget)
{
	GSWindow             *window = GS_WINDOW (widget);
	GSWindowWaylandPrivate *priv;

	priv = GS_WINDOW_WAYLAND_GET_PRIVATE (window);

	remove_watchdog_timer (window);

	if (priv->lock_surface != NULL)
	{
		ext_session_lock_surface_v1_destroy (priv->lock_surface);
		priv->lock_surface = NULL;
	}

	if (priv->session_lock != NULL)
	{
		ext_session_lock_v1_destroy (priv->session_lock);
		priv->session_lock = NULL;
		priv->is_locked = FALSE;
		priv->locked_confirmed = FALSE;
	}

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

	if (priv->lock_surface != NULL)
	{
		ext_session_lock_surface_v1_destroy (priv->lock_surface);
		priv->lock_surface = NULL;
	}

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

	priv->wl_surface = NULL;
	priv->session_lock = NULL;
	priv->lock_surface = NULL;
	priv->is_locked = FALSE;
	priv->locked_confirmed = FALSE;
	priv->idle_notification = NULL;
	priv->idle_timeout_id = 0;
	priv->watchdog_timer_id = 0;
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
	GSWindow             *window;
	GSWindowWaylandPrivate *priv;

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

	if (priv->lock_surface != NULL)
	{
		ext_session_lock_surface_v1_destroy (priv->lock_surface);
		priv->lock_surface = NULL;
	}

	if (priv->session_lock != NULL)
	{
		ext_session_lock_v1_destroy (priv->session_lock);
		priv->session_lock = NULL;
	}

	priv->is_locked = FALSE;
	priv->locked_confirmed = FALSE;

	/* Note: common fields (logout_command, keyboard_command, status_message)
	 * are freed by the base class finalize */

	G_OBJECT_CLASS (gs_window_wayland_parent_class)->finalize (object);
}

void
gs_window_wayland_set_session_lock (GSWindow                   *window,
                                    struct ext_session_lock_v1 *session_lock)
{
	GSWindowWaylandPrivate *priv;

	g_return_if_fail (GS_IS_WINDOW_WAYLAND (window));

	priv = GS_WINDOW_WAYLAND_GET_PRIVATE (window);

	if (priv->session_lock != NULL)
	{
		ext_session_lock_v1_destroy (priv->session_lock);
	}

	priv->session_lock = session_lock;

	if (priv->session_lock != NULL)
	{
		ext_session_lock_v1_add_listener (priv->session_lock,
		                                 &session_lock_listener,
		                                 window);
	}
}

struct ext_session_lock_v1 *
gs_window_wayland_get_session_lock (GSWindow *window)
{
	GSWindowWaylandPrivate *priv;

	g_return_val_if_fail (GS_IS_WINDOW_WAYLAND (window), NULL);

	priv = GS_WINDOW_WAYLAND_GET_PRIVATE (window);

	return priv->session_lock;
}

gboolean
gs_window_wayland_is_locked (GSWindow *window)
{
	GSWindowWaylandPrivate *priv;

	g_return_val_if_fail (GS_IS_WINDOW_WAYLAND (window), FALSE);

	priv = GS_WINDOW_WAYLAND_GET_PRIVATE (window);

	return priv->is_locked;
}

gboolean
gs_window_wayland_is_lock_confirmed (GSWindow *window)
{
	GSWindowWaylandPrivate *priv;

	g_return_val_if_fail (GS_IS_WINDOW_WAYLAND (window), FALSE);

	priv = GS_WINDOW_WAYLAND_GET_PRIVATE (window);

	return priv->locked_confirmed;
}

void
gs_window_wayland_create_lock_surface (GSWindow *window)
{
	GSWindowWaylandPrivate *priv;
	GdkWindow              *gdk_window;

	g_return_if_fail (GS_IS_WINDOW_WAYLAND (window));

	priv = GS_WINDOW_WAYLAND_GET_PRIVATE (window);

	if (priv->session_lock == NULL)
	{
		gs_debug ("No session lock available");
		return;
	}

	if (priv->lock_surface != NULL)
	{
		gs_debug ("Lock surface already exists");
		return;
	}

	gdk_window = gtk_widget_get_window (GTK_WIDGET (window));
	if (gdk_window == NULL)
	{
		gs_debug ("No GDK window available");
		return;
	}

	priv->wl_surface = gdk_wayland_window_get_wl_surface (gdk_window);
	if (priv->wl_surface == NULL)
	{
		gs_debug ("Failed to get Wayland surface");
		return;
	}

	priv->lock_surface = ext_session_lock_v1_get_lock_surface (priv->session_lock,
	                                                           priv->wl_surface,
	                                                           /* output */
	                                                           NULL);
	if (priv->lock_surface != NULL)
	{
		ext_session_lock_surface_v1_add_listener (priv->lock_surface,
		                                         &lock_surface_listener,
		                                         window);
		gs_debug ("Lock surface created");
	}
	else
	{
		gs_debug ("Failed to create lock surface");
	}
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
