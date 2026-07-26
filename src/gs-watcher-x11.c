/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2004-2006 William Jon McCann <mccann@jhu.edu>
 * Copyright (C) 2008      Red Hat, Inc.
 * Copyright (C) 2012-2026 MATE Developers
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
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
#include <stdio.h>
#include <time.h>
#include <errno.h>

#include <string.h>
#include <gdk/gdkx.h>

#include <dbus/dbus.h>
#include <dbus/dbus-glib.h>

#include "gs-watcher.h"
#include "gs-watcher-private.h"
#include "gs-marshal.h"
#include "gs-debug.h"

typedef struct GSWatcherX11Private GSWatcherX11Private;

struct GSWatcherX11Private
{
	DBusGProxy     *presence_proxy;
	guint           watchdog_timer_id;
	guint           idle_id;
	guint           delta_notice_timeout;
};

typedef struct
{
	GSWatcher parent;
} GSWatcherX11;

typedef struct
{
	GSWatcherClass parent_class;
} GSWatcherX11Class;

G_DEFINE_TYPE_WITH_PRIVATE (GSWatcherX11, gs_watcher_x11, GS_TYPE_WATCHER)

#define GS_WATCHER_X11(o)      (G_TYPE_CHECK_INSTANCE_CAST ((o), GS_TYPE_WATCHER_X11, GSWatcherX11))
#define GS_IS_WATCHER_X11(o)   (G_TYPE_CHECK_INSTANCE_TYPE ((o), GS_TYPE_WATCHER_X11))
#define GS_TYPE_WATCHER_X11    (gs_watcher_x11_get_type ())

#define WATCHER_X11_GET_PRIVATE(o) ((GSWatcherX11Private *)gs_watcher_x11_get_instance_private (GS_WATCHER_X11 (o)))

static void remove_watchdog_timer (GSWatcherX11 *x11);
static gboolean watchdog_timer (GSWatcherX11 *x11);

static void
remove_idle_id (GSWatcherX11 *x11)
{
	GSWatcherX11Private *priv = WATCHER_X11_GET_PRIVATE (x11);
	if (priv->idle_id > 0)
	{
		g_source_remove (priv->idle_id);
		priv->idle_id = 0;
	}
}

static void
add_watchdog_timer (GSWatcherX11 *x11,
                    guint         timeout)
{
	GSWatcherX11Private *priv = WATCHER_X11_GET_PRIVATE (x11);
	priv->watchdog_timer_id = g_timeout_add (timeout,
	                                        (GSourceFunc)watchdog_timer,
	                                        x11);
}

static void
remove_watchdog_timer (GSWatcherX11 *x11)
{
	GSWatcherX11Private *priv = WATCHER_X11_GET_PRIVATE (x11);
	if (priv->watchdog_timer_id != 0)
	{
		g_source_remove (priv->watchdog_timer_id);
		priv->watchdog_timer_id = 0;
	}
}

static void
on_idle_timeout (GSWatcherX11 *x11)
{
	GSWatcher *watcher = GS_WATCHER (x11);
	GSWatcherX11Private *priv = WATCHER_X11_GET_PRIVATE (x11);
	gboolean res;

	res = _gs_watcher_set_session_idle (watcher, TRUE);

	_gs_watcher_set_session_idle_notice (watcher, FALSE);

	/* try again if we failed i guess */
	if (res)
	{
		priv->idle_id = 0;
	}
}

static void
set_status (GSWatcherX11 *x11,
            guint         status)
{
	GSWatcher *watcher = GS_WATCHER (x11);
	GSWatcherX11Private *priv = WATCHER_X11_GET_PRIVATE (x11);
	gboolean is_idle;

	if (! watcher->priv->active)
	{
		gs_debug ("GSWatcher: not active, ignoring status changes");
		/* no change in idleness */
		return;
	}

	is_idle = (status == 3);

	if (!is_idle && !watcher->priv->idle_notice)
	{
		return;
	}

	if (is_idle)
	{
		_gs_watcher_set_session_idle_notice (watcher, is_idle);
		/* queue an activation */
		if (priv->idle_id > 0)
		{
			g_source_remove (priv->idle_id);
		}
		priv->idle_id = g_timeout_add (priv->delta_notice_timeout,
		                              (GSourceFunc)on_idle_timeout,
		                              x11);
	}
	/* cancel notice too */
	else
	{
		remove_idle_id (x11);
		_gs_watcher_set_session_idle (watcher, FALSE);
		_gs_watcher_set_session_idle_notice (watcher, FALSE);
	}
}

static void
on_presence_status_changed (DBusGProxy    *presence_proxy,
                            guint          status,
                            GSWatcherX11  *x11)
{
	set_status (x11, status);
}

static void
on_presence_status_text_changed (DBusGProxy    *presence_proxy,
                                 const char    *status_text,
                                 GSWatcherX11  *x11)
{
	GSWatcher *watcher = GS_WATCHER (x11);
	g_object_set (watcher, "status-message", status_text, NULL);
}

static gboolean
connect_presence_watcher (GSWatcherX11 *x11)
{
	DBusGConnection   *bus;
	GError            *error;
	gboolean           ret;

	ret = FALSE;

	error = NULL;
	bus = dbus_g_bus_get (DBUS_BUS_SESSION, &error);
	if (bus == NULL)
	{
		g_warning ("Unable to get session bus: %s", error->message);
		g_error_free (error);
		goto done;
	}

	error = NULL;
	WATCHER_X11_GET_PRIVATE(x11)->presence_proxy = dbus_g_proxy_new_for_name_owner (bus,
	                                "org.gnome.SessionManager",
	                                "/org/gnome/SessionManager/Presence",
	                                "org.gnome.SessionManager.Presence",
	                                &error);
	if (WATCHER_X11_GET_PRIVATE(x11)->presence_proxy != NULL)
	{
		DBusGProxy *proxy;

		dbus_g_proxy_add_signal (WATCHER_X11_GET_PRIVATE(x11)->presence_proxy,
		                         "StatusChanged",
		                         G_TYPE_UINT,
		                         G_TYPE_INVALID);
		dbus_g_proxy_connect_signal (WATCHER_X11_GET_PRIVATE(x11)->presence_proxy,
		                             "StatusChanged",
		                             G_CALLBACK (on_presence_status_changed),
		                             x11,
		                             NULL);
		dbus_g_proxy_add_signal (WATCHER_X11_GET_PRIVATE(x11)->presence_proxy,
		                         "StatusTextChanged",
		                         G_TYPE_STRING,
		                         G_TYPE_INVALID);
		dbus_g_proxy_connect_signal (WATCHER_X11_GET_PRIVATE(x11)->presence_proxy,
		                             "StatusTextChanged",
		                             G_CALLBACK (on_presence_status_text_changed),
		                             x11,
		                             NULL);

		proxy = dbus_g_proxy_new_from_proxy (WATCHER_X11_GET_PRIVATE(x11)->presence_proxy,
		                                     "org.freedesktop.DBus.Properties",
		                                     "/org/gnome/SessionManager/Presence");
		if (proxy != NULL)
		{
			guint       status;
			const char *status_text;
			GValue      value = { 0, };

			status = 0;
			status_text = NULL;

			error = NULL;
			dbus_g_proxy_call (proxy,
			                   "Get",
			                   &error,
			                   G_TYPE_STRING, "org.gnome.SessionManager.Presence",
			                   G_TYPE_STRING, "status",
			                   G_TYPE_INVALID,
			                   G_TYPE_VALUE, &value,
			                   G_TYPE_INVALID);

			if (error != NULL)
			{
				g_warning ("Couldn't get presence status: %s", error->message);
				g_error_free (error);
				goto done;
			}
			else
			{
				status = g_value_get_uint (&value);
			}

			g_value_unset (&value);

			error = NULL;
			dbus_g_proxy_call (proxy,
			                   "Get",
			                   &error,
			                   G_TYPE_STRING, "org.gnome.SessionManager.Presence",
			                   G_TYPE_STRING, "status-text",
			                   G_TYPE_INVALID,
			                   G_TYPE_VALUE, &value,
			                   G_TYPE_INVALID);

			if (error != NULL)
			{
				g_warning ("Couldn't get presence status text: %s", error->message);
				g_error_free (error);
			}
			else
			{
				status_text = g_value_get_string (&value);
			}

			set_status (x11, status);
			g_object_set (GS_WATCHER (x11), "status-message", status_text, NULL);
		}
	}
	else
	{
		g_warning ("Failed to get session presence proxy: %s", error->message);
		g_error_free (error);
		goto done;
	}

	ret = TRUE;

done:
	return ret;
}

/* Figuring out what the appropriate XSetScreenSaver() parameters are
   (one wouldn't expect this to be rocket science.)
*/
static void
disable_builtin_screensaver (gboolean unblank_screen)
{
	int current_server_timeout, current_server_interval;
	int current_prefer_blank,   current_allow_exp;
	int desired_server_timeout, desired_server_interval;
	int desired_prefer_blank,   desired_allow_exp;

	XGetScreenSaver (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()),
	                 &current_server_timeout,
	                 &current_server_interval,
	                 &current_prefer_blank,
	                 &current_allow_exp);

	desired_server_timeout  = current_server_timeout;
	desired_server_interval = current_server_interval;
	desired_prefer_blank    = current_prefer_blank;
	desired_allow_exp       = current_allow_exp;

	desired_server_interval = 0;
	/* I suspect (but am not sure) that DontAllowExposures might have
	   something to do with powering off the monitor as well, at least
	   on some systems that don't support XDPMS?  Who know... */
	desired_allow_exp = AllowExposures;
	/* When we're not using an extension, set the server-side timeout to 0,
	   so that the server never gets involved with screen blanking, and we
	   do it all ourselves.  (However, when we *are* using an extension,
	   we tell the server when to notify us, and rather than blanking the
	   screen, the server will send us an X event telling us to blank.)
	*/
	desired_server_timeout = 0;

	if (desired_server_timeout     != current_server_timeout
	        || desired_server_interval != current_server_interval
	        || desired_prefer_blank    != current_prefer_blank
	        || desired_allow_exp       != current_allow_exp)
	{
		gs_debug ("disabling server builtin screensaver:"
		          " (xset s %d %d; xset s %s; xset s %s)",
		          desired_server_timeout,
		          desired_server_interval,
		          (desired_prefer_blank ? "blank" : "noblank"),
		          (desired_allow_exp ? "expose" : "noexpose"));

		XSetScreenSaver (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()),
		                 desired_server_timeout,
		                 desired_server_interval,
		                 desired_prefer_blank,
		                 desired_allow_exp);

		XSync (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()), FALSE);
	}

	if (unblank_screen)
	{
		/* Turn off the server builtin saver if it is now running. */
		XForceScreenSaver (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()), ScreenSaverReset);
	}
}

/* This timer goes off every few minutes, whether the user is idle or not,
   to try and clean up anything that has gone wrong.

   It calls disable_builtin_screensaver() so that if xset has been used,
   or some other program (like xlock) has messed with the XSetScreenSaver()
   settings, they will be set back to sensible values (if a server extension
   is in use, messing with xlock can cause the screensaver to never get a wakeup
   event, and could cause monitor power-saving to occur, and all manner of
   heinousness.)

 */
static gboolean
watchdog_timer (GSWatcherX11 *x11)
{
	disable_builtin_screensaver (FALSE);
	return TRUE;
}

static void
gs_watcher_x11_activate_monitoring (GSWatcher *watcher,
                                    guint      timeout_ms)
{
	GSWatcherX11 *x11 = GS_WATCHER_X11 (watcher);

	gs_debug ("X11: activating idle monitoring");

	disable_builtin_screensaver (TRUE);
	add_watchdog_timer (x11, 600000);
}

static void
gs_watcher_x11_deactivate_monitoring (GSWatcher *watcher)
{
	GSWatcherX11 *x11 = GS_WATCHER_X11 (watcher);

	gs_debug ("X11: deactivating idle monitoring");

	remove_idle_id (x11);
	remove_watchdog_timer (x11);
}

static void
gs_watcher_x11_class_init (GSWatcherX11Class *klass)
{
	GSWatcherClass *watcher_class = GS_WATCHER_CLASS (klass);

	watcher_class->activate_monitoring = gs_watcher_x11_activate_monitoring;
	watcher_class->deactivate_monitoring = gs_watcher_x11_deactivate_monitoring;
}

static void
gs_watcher_x11_init (GSWatcherX11 *x11)
{
	GSWatcherX11Private *priv = WATCHER_X11_GET_PRIVATE (x11);

	priv->presence_proxy = NULL;
	priv->watchdog_timer_id = 0;
	priv->idle_id = 0;
	priv->delta_notice_timeout = 10000;

	connect_presence_watcher (x11);
}

static void
gs_watcher_x11_finalize (GObject *object)
{
	GSWatcherX11 *x11;
	GSWatcherX11Private *priv;

	g_return_if_fail (object != NULL);
	g_return_if_fail (GS_IS_WATCHER_X11 (object));

	x11 = GS_WATCHER_X11 (object);
	priv = WATCHER_X11_GET_PRIVATE (x11);

	remove_idle_id (x11);
	remove_watchdog_timer (x11);

	if (priv->presence_proxy != NULL)
	{
		g_object_unref (priv->presence_proxy);
		priv->presence_proxy = NULL;
	}

	G_OBJECT_CLASS (gs_watcher_x11_parent_class)->finalize (object);
}
