/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2004-2006 William Jon McCann <mccann@jhu.edu>
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
 * Authors: William Jon McCann <mccann@jhu.edu>
 *
 */

#include "config.h"
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include <gtk/gtk.h>

#include "gs-window.h"
#include "gs-grab.h"
#include "gs-debug.h"

typedef struct GSGrabX11Private GSGrabX11Private;

struct GSGrabX11Private
{
	GdkWindow  *grab_window;
	GdkDisplay *grab_display;
	guint       no_pointer_grab : 1;
	guint       hide_cursor : 1;

	GtkWidget *invisible;
};

typedef struct
{
	GSGrab parent;
} GSGrabX11;

typedef struct
{
	GSGrabClass parent_class;
} GSGrabX11Class;

G_DEFINE_TYPE_WITH_PRIVATE (GSGrabX11, gs_grab_x11, GS_TYPE_GRAB)

#define GS_GRAB_X11(o)      (G_TYPE_CHECK_INSTANCE_CAST ((o), GS_TYPE_GRAB_X11, GSGrabX11))
#define GS_IS_GRAB_X11(o)   (G_TYPE_CHECK_INSTANCE_TYPE ((o), GS_TYPE_GRAB_X11))
#define GS_TYPE_GRAB_X11    (gs_grab_x11_get_type ())

#define GRAB_X11_GET_PRIVATE(o) ((GSGrabX11Private *)gs_grab_x11_get_instance_private (GS_GRAB_X11 (o)))

static const char *
grab_string (int status)
{
	switch (status)
	{
	case GDK_GRAB_SUCCESS:
		return "GrabSuccess";
	case GDK_GRAB_ALREADY_GRABBED:
		return "AlreadyGrabbed";
	case GDK_GRAB_INVALID_TIME:
		return "GrabInvalidTime";
	case GDK_GRAB_NOT_VIEWABLE:
		return "GrabNotViewable";
	case GDK_GRAB_FROZEN:
		return "GrabFrozen";
	case GDK_GRAB_FAILED:
		return "GrabFailed";
	default:
	{
		static char foo [255];
		sprintf (foo, "unknown status: %d", status);
		return foo;
	}
	}
}

static void
xorg_lock_smasher_set_active (GSGrab  *grab,
                              gboolean active)
{
}

static void
prepare_window_grab_cb (GdkSeat   *seat,
                        GdkWindow *window,
                        gpointer   user_data)
{
	gdk_window_show_unraised (window);
}

static int
gs_grab_get (GSGrab     *grab,
             GdkWindow  *window,
             GdkDisplay *display,
             gboolean    no_pointer_grab,
             gboolean    hide_cursor)
{
	GSGrabX11Private *priv = GRAB_X11_GET_PRIVATE (grab);
	GdkGrabStatus status;
	GdkSeat      *seat;
	GdkSeatCapabilities caps;
	GdkCursor    *cursor;

	g_return_val_if_fail (window != NULL, FALSE);
	g_return_val_if_fail (display != NULL, FALSE);

	cursor = gdk_cursor_new_for_display (display, GDK_BLANK_CURSOR);

	gs_debug ("Grabbing devices for window=%X", (guint32) GDK_WINDOW_XID (window));

	seat = gdk_display_get_default_seat (display);
	if (!no_pointer_grab)
		caps = GDK_SEAT_CAPABILITY_ALL;
	else
		caps = GDK_SEAT_CAPABILITY_KEYBOARD;

	status = gdk_seat_grab (seat, window,
	                        caps, TRUE,
	                        (hide_cursor ? cursor : NULL),
	                        NULL,
	                        prepare_window_grab_cb,
	                        NULL);

	if (status == GDK_GRAB_SUCCESS && no_pointer_grab &&
	    gdk_display_device_is_grabbed (display, gdk_seat_get_pointer (seat)))
	{
		gs_grab_release (grab, FALSE);
		gs_debug ("Regrabbing keyboard");
		status = gdk_seat_grab (seat, window,
		                        caps, TRUE,
		                        (hide_cursor ? cursor : NULL),
		                        NULL, NULL, NULL);
	}

	if (status == GDK_GRAB_SUCCESS)
	{
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

	g_object_unref (G_OBJECT (cursor));

	return status;
}

static void
x11_grab_reset (GSGrab *grab)
{
	GSGrabX11Private *priv = GRAB_X11_GET_PRIVATE (grab);

	if (priv->grab_window != NULL)
	{
		g_object_remove_weak_pointer (G_OBJECT (priv->grab_window),
		                              (gpointer *) &priv->grab_window);
	}
	priv->grab_window = NULL;
	priv->grab_display = NULL;
}

static void
x11_grab_release (GSGrab *grab, gboolean flush)
{
	GdkDisplay *display;
	GdkSeat    *seat;

	display = gdk_display_get_default ();
	seat = gdk_display_get_default_seat (display);

	gs_debug ("Ungrabbing devices");

	gdk_seat_ungrab (seat);

	x11_grab_reset (grab);

	if (flush)
	{
		xorg_lock_smasher_set_active (grab, TRUE);

		gdk_display_sync (display);
		gdk_display_flush (display);
	}
}

static gboolean
gs_grab_move (GSGrab     *grab,
              GdkWindow  *window,
              GdkDisplay *display,
              gboolean    no_pointer_grab,
              gboolean    hide_cursor)
{
	GSGrabX11Private *priv = GRAB_X11_GET_PRIVATE (grab);
	int         result;
	GdkWindow  *old_window;
	GdkDisplay *old_display;
	gboolean    old_hide_cursor;

	if (priv->grab_window == window &&
	    priv->no_pointer_grab == no_pointer_grab)
	{
		gs_debug ("Window %X is already grabbed, skipping",
		          (guint32) GDK_WINDOW_XID (priv->grab_window));
		return TRUE;
	}

	if (priv->grab_window != NULL)
	{
		gs_debug ("Moving devices grab from %X to %X",
		          (guint32) GDK_WINDOW_XID (priv->grab_window),
		          (guint32) GDK_WINDOW_XID (window));
	}
	else
	{
		gs_debug ("Getting devices grab on %X",
		          (guint32) GDK_WINDOW_XID (window));
	}

	gs_debug ("*** doing X server grab");
	gdk_x11_display_grab (display);

	old_window = priv->grab_window;
	old_display = priv->grab_display;
	old_hide_cursor = priv->hide_cursor;

	if (old_window)
	{
		x11_grab_release (grab, FALSE);
	}

	result = gs_grab_get (grab, window, display,
	                      no_pointer_grab, hide_cursor);

	if (result != GDK_GRAB_SUCCESS)
	{
		g_usleep (G_USEC_PER_SEC);
		result = gs_grab_get (grab, window, display,
		                      no_pointer_grab, hide_cursor);
	}

	if ((result != GDK_GRAB_SUCCESS) && old_window)
	{
		int old_result;

		gs_debug ("Could not grab devices for new window. Resuming previous grab.");
		old_result = gs_grab_get (grab, old_window, old_display,
		                          no_pointer_grab, old_hide_cursor);
		if (old_result != GDK_GRAB_SUCCESS)
			gs_debug ("Could not grab devices for old window");
	}

	gs_debug ("*** releasing X server grab");
	gdk_x11_display_ungrab (display);
	gdk_display_flush (display);

	return (result == GDK_GRAB_SUCCESS);
}

static void
gs_grab_nuke_focus (GdkDisplay *display)
{
	Window focus = 0;
	int    rev = 0;

	gs_debug ("Nuking focus");

	gdk_x11_display_error_trap_push (display);

	XGetInputFocus (GDK_DISPLAY_XDISPLAY (display), &focus, &rev);
	XSetInputFocus (GDK_DISPLAY_XDISPLAY (display), None,
	                RevertToNone, CurrentTime);

	gdk_x11_display_error_trap_pop_ignored (display);
}

static gboolean
x11_grab_grab_window (GSGrab     *grab,
                      GdkWindow  *window,
                      GdkDisplay *display,
                      gboolean    no_pointer_grab,
                      gboolean    hide_cursor)
{
	gboolean    status = FALSE;
	int         i;
	int         retries = 12;

	for (i = 0; i < retries; i++)
	{
		status = gs_grab_get (grab, window, display,
		                      no_pointer_grab, hide_cursor);
		if (status == GDK_GRAB_SUCCESS)
		{
			break;
		}
		else if (i == (int) (retries / 2))
		{
			gs_grab_nuke_focus (display);
		}

		g_usleep (G_USEC_PER_SEC);
	}

	if (status != GDK_GRAB_SUCCESS)
	{
		gs_debug ("Couldn't grab devices!  (%s)",
		          grab_string (status));
		return FALSE;
	}

	return TRUE;
}

static gboolean
x11_grab_grab_root (GSGrab  *grab,
                    gboolean no_pointer_grab,
                    gboolean hide_cursor)
{
	GdkDisplay *display;
	GdkWindow  *root;
	GdkScreen  *screen;
	GdkDevice  *device;
	gboolean    res;

	gs_debug ("Grabbing the root window");

	display = gdk_display_get_default ();
	device = gdk_seat_get_pointer (gdk_display_get_default_seat (display));
	gdk_device_get_position (device, &screen, NULL, NULL);
	root = gdk_screen_get_root_window (screen);

	res = x11_grab_grab_window (grab, root, display,
	                            no_pointer_grab, hide_cursor);

	return res;
}

static gboolean
x11_grab_grab_offscreen (GSGrab *grab,
                         gboolean no_pointer_grab,
                         gboolean hide_cursor)
{
	GSGrabX11Private *priv = GRAB_X11_GET_PRIVATE (grab);
	GdkWindow *window;
	GdkDisplay *display;
	GdkScreen  *screen;
	gboolean    res;

	gs_debug ("Grabbing an offscreen window");

	window = gtk_widget_get_window (GTK_WIDGET (priv->invisible));
	screen = gtk_invisible_get_screen (GTK_INVISIBLE (priv->invisible));
	display = gdk_screen_get_display (screen);
	res = x11_grab_grab_window (grab, window, display,
	                            no_pointer_grab, hide_cursor);

	return res;
}

static void
x11_grab_move_to_window (GSGrab     *grab,
                         GdkWindow  *window,
                         GdkDisplay *display,
                         gboolean    no_pointer_grab,
                         gboolean    hide_cursor)
{
	gboolean result = FALSE;

	g_return_if_fail (GS_IS_GRAB (grab));

	xorg_lock_smasher_set_active (grab, FALSE);

	while (!result)
	{
		result = gs_grab_move (grab, window, display,
		                       no_pointer_grab, hide_cursor);
		gdk_display_flush (display);
	}
}

static void
gs_grab_x11_class_init (GSGrabX11Class *klass)
{
	GSGrabClass *grab_class = GS_GRAB_CLASS (klass);

	grab_class->release = x11_grab_release;
	grab_class->grab_window = x11_grab_grab_window;
	grab_class->grab_root = x11_grab_grab_root;
	grab_class->grab_offscreen = x11_grab_grab_offscreen;
	grab_class->move_to_window = x11_grab_move_to_window;
	grab_class->reset = x11_grab_reset;
}

static void
gs_grab_x11_init (GSGrabX11 *x11)
{
	GSGrabX11Private *priv;

	priv = gs_grab_x11_get_instance_private (x11);

	priv->no_pointer_grab = FALSE;
	priv->hide_cursor = FALSE;
	priv->invisible = gtk_invisible_new ();
	gtk_widget_show (priv->invisible);
}


