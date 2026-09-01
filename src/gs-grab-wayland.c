/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2012-2026 MATE Developers
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 */

#include "config.h"

#include <gtk/gtk.h>

#ifdef ENABLE_WAYLAND
#include <gdk/gdkwayland.h>
#endif

#include "gs-grab.h"
#include "gs-debug.h"

typedef struct GSGrabWaylandPrivate GSGrabWaylandPrivate;

struct GSGrabWaylandPrivate
{
	GdkWindow  *grab_window;
	GdkDisplay *grab_display;
	guint       no_pointer_grab : 1;
	guint       hide_cursor : 1;
};

typedef struct
{
	GSGrab parent;
} GSGrabWayland;

typedef struct
{
	GSGrabClass parent_class;
} GSGrabWaylandClass;

G_DEFINE_TYPE_WITH_PRIVATE (GSGrabWayland, gs_grab_wayland, GS_TYPE_GRAB)

#define GS_GRAB_WAYLAND(o)      (G_TYPE_CHECK_INSTANCE_CAST ((o), GS_TYPE_GRAB_WAYLAND, GSGrabWayland))
#define GS_IS_GRAB_WAYLAND(o)   (G_TYPE_CHECK_INSTANCE_TYPE ((o), GS_TYPE_GRAB_WAYLAND))
#define GS_TYPE_GRAB_WAYLAND    (gs_grab_wayland_get_type ())

#define GS_GRAB_WAYLAND_GET_PRIVATE(o) ((GSGrabWaylandPrivate *)gs_grab_wayland_get_instance_private (GS_GRAB_WAYLAND (o)))

static void
wayland_grab_reset (GSGrab *grab)
{
	GSGrabWaylandPrivate *priv = GS_GRAB_WAYLAND_GET_PRIVATE (grab);

	if (priv->grab_window != NULL)
	{
		g_object_remove_weak_pointer (G_OBJECT (priv->grab_window),
		                              (gpointer *) &priv->grab_window);
	}
	priv->grab_window = NULL;
	priv->grab_display = NULL;
}

static void
wayland_grab_release (GSGrab *grab, gboolean flush)
{
	GSGrabWaylandPrivate *priv = GS_GRAB_WAYLAND_GET_PRIVATE (grab);

	gs_debug ("Releasing Wayland grab (compositor handles input)");

	if (priv->grab_window != NULL)
	{
		g_object_remove_weak_pointer (G_OBJECT (priv->grab_window),
		                              (gpointer *) &priv->grab_window);
	}
	priv->grab_window = NULL;
	priv->grab_display = NULL;

	if (flush)
	{
		GdkDisplay *display;

		display = gdk_display_get_default ();
		gdk_display_flush (display);
	}
}

static gboolean
wayland_grab_grab_window (GSGrab     *grab,
                          GdkWindow  *window,
                          GdkDisplay *display,
                          gboolean    no_pointer_grab,
                          gboolean    hide_cursor)
{
	GSGrabWaylandPrivate *priv = GS_GRAB_WAYLAND_GET_PRIVATE (grab);

	g_return_val_if_fail (window != NULL, FALSE);
	g_return_val_if_fail (display != NULL, FALSE);

	gs_debug ("Wayland: storing window reference (compositor handles grab)");

	if (priv->grab_window != NULL)
	{
		g_object_remove_weak_pointer (G_OBJECT (priv->grab_window),
		                              (gpointer *) &priv->grab_window);
	}

	priv->grab_window = window;
	g_object_add_weak_pointer (G_OBJECT (priv->grab_window),
	                           (gpointer *) &priv->grab_window);

	priv->grab_display = display;
	priv->no_pointer_grab = (no_pointer_grab != FALSE);
	priv->hide_cursor = (hide_cursor != FALSE);

	return TRUE;
}

static gboolean
wayland_grab_grab_root (GSGrab  *grab,
                        gboolean no_pointer_grab,
                        gboolean hide_cursor)
{
	GdkDisplay *display;
	GdkWindow  *root;
	GdkScreen  *screen;
	GdkDevice  *device;

	gs_debug ("Wayland: grabbing the root window");

	display = gdk_display_get_default ();
	device = gdk_seat_get_pointer (gdk_display_get_default_seat (display));
	gdk_device_get_position (device, &screen, NULL, NULL);
	root = gdk_screen_get_root_window (screen);

	return wayland_grab_grab_window (grab, root, display,
	                                 no_pointer_grab, hide_cursor);
}

static gboolean
wayland_grab_grab_offscreen (GSGrab    *grab,
                             gboolean   no_pointer_grab,
                             gboolean   hide_cursor)
{
	GtkWidget *invisible;
	GdkWindow  *window;
	GdkDisplay *display;
	GdkScreen  *screen;

	gs_debug ("Wayland: grabbing an offscreen window");

	invisible = gtk_invisible_new ();
	gtk_widget_show (invisible);

	window = gtk_widget_get_window (invisible);
	screen = gtk_invisible_get_screen (GTK_INVISIBLE (invisible));
	display = gdk_screen_get_display (screen);

	return wayland_grab_grab_window (grab, window, display,
	                                 no_pointer_grab, hide_cursor);
}

static void
wayland_grab_move_to_window (GSGrab     *grab,
                             GdkWindow  *window,
                             GdkDisplay *display,
                             gboolean    no_pointer_grab,
                             gboolean    hide_cursor)
{
	GSGrabWaylandPrivate *priv = GS_GRAB_WAYLAND_GET_PRIVATE (grab);

	g_return_if_fail (GS_IS_GRAB (grab));

	gs_debug ("Wayland: moving grab to new window");

	if (priv->grab_window == window &&
	    priv->no_pointer_grab == no_pointer_grab)
	{
		gs_debug ("Window is already grabbed, skipping");
		return;
	}

	if (priv->grab_window != NULL)
	{
		g_object_remove_weak_pointer (G_OBJECT (priv->grab_window),
		                              (gpointer *) &priv->grab_window);
	}

	priv->grab_window = window;
	g_object_add_weak_pointer (G_OBJECT (priv->grab_window),
	                           (gpointer *) &priv->grab_window);

	priv->grab_display = display;
	priv->no_pointer_grab = (no_pointer_grab != FALSE);
	priv->hide_cursor = (hide_cursor != FALSE);
}

static void
gs_grab_wayland_class_init (GSGrabWaylandClass *klass)
{
	GSGrabClass *grab_class = GS_GRAB_CLASS (klass);

	grab_class->release = wayland_grab_release;
	grab_class->grab_window = wayland_grab_grab_window;
	grab_class->grab_root = wayland_grab_grab_root;
	grab_class->grab_offscreen = wayland_grab_grab_offscreen;
	grab_class->move_to_window = wayland_grab_move_to_window;
	grab_class->reset = wayland_grab_reset;
}

static void
gs_grab_wayland_init (GSGrabWayland *wayland)
{
	GSGrabWaylandPrivate *priv;

	priv = gs_grab_wayland_get_instance_private (wayland);

	priv->grab_window = NULL;
	priv->grab_display = NULL;
	priv->no_pointer_grab = FALSE;
	priv->hide_cursor = FALSE;
}

GSGrab *
gs_grab_wayland_new (void)
{
	return g_object_new (GS_TYPE_GRAB_WAYLAND, NULL);
}
