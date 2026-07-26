/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
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
 */

#include "config.h"

#include <gtk/gtk.h>

#ifdef ENABLE_WAYLAND
#include <gdk/gdkwayland.h>
#include "ext-idle-notify-client.h"
#endif

#include "gs-watcher.h"
#include "gs-watcher-private.h"
#include "gs-debug.h"

typedef struct GSWatcherWaylandPrivate GSWatcherWaylandPrivate;

struct GSWatcherWaylandPrivate
{
	struct ext_idle_notification_v1  *idle_notification;
	struct ext_idle_notifier_v1      *idle_notifier;
	guint                            idle_timeout_id;
};

typedef struct
{
	GSWatcher parent;
} GSWatcherWayland;

typedef struct
{
	GSWatcherClass parent_class;
} GSWatcherWaylandClass;

G_DEFINE_TYPE_WITH_PRIVATE (GSWatcherWayland, gs_watcher_wayland, GS_TYPE_WATCHER)

#define GS_WATCHER_WAYLAND(o)      (G_TYPE_CHECK_INSTANCE_CAST ((o), GS_TYPE_WATCHER_WAYLAND, GSWatcherWayland))
#define GS_IS_WATCHER_WAYLAND(o)   (G_TYPE_CHECK_INSTANCE_TYPE ((o), GS_TYPE_WATCHER_WAYLAND))
#define GS_TYPE_WATCHER_WAYLAND    (gs_watcher_wayland_get_type ())

#define GS_WATCHER_WAYLAND_GET_PRIVATE(o) ((GSWatcherWaylandPrivate *)gs_watcher_wayland_get_instance_private (GS_WATCHER_WAYLAND (o)))

static void remove_idle_notification (GSWatcherWayland *wayland);

static void
on_idled (void                            *data,
          struct ext_idle_notification_v1 *notification)
{
	GSWatcherWayland *wayland = GS_WATCHER_WAYLAND (data);
	GSWatcher        *watcher = GS_WATCHER (wayland);

	gs_debug ("Wayland: idle notification received");

	_gs_watcher_set_session_idle (watcher, TRUE);
	_gs_watcher_set_session_idle_notice (watcher, FALSE);
}

static void
on_resumed (void                            *data,
            struct ext_idle_notification_v1 *notification)
{
	GSWatcherWayland *wayland = GS_WATCHER_WAYLAND (data);
	GSWatcher        *watcher = GS_WATCHER (wayland);

	gs_debug ("Wayland: resumed notification received");

	_gs_watcher_set_session_idle (watcher, FALSE);
	_gs_watcher_set_session_idle_notice (watcher, FALSE);
}

static const struct ext_idle_notification_v1_listener idle_notification_listener =
{
	.idled = on_idled,
	.resumed = on_resumed,
};

static struct ext_idle_notifier_v1 *
bind_idle_notifier (struct wl_registry *registry,
                    guint32             name)
{
	struct ext_idle_notifier_v1 *notifier;

	notifier = wl_registry_bind (registry,
	                             name,
	                             &ext_idle_notifier_v1_interface,
	                             1);

	return notifier;
}

static void
remove_idle_notification (GSWatcherWayland *wayland)
{
	GSWatcherWaylandPrivate *priv;

	priv = GS_WATCHER_WAYLAND_GET_PRIVATE (wayland);

	if (priv->idle_notification != NULL)
	{
		ext_idle_notification_v1_destroy (priv->idle_notification);
		priv->idle_notification = NULL;
	}
}

static void
remove_idle_notifier (GSWatcherWayland *wayland)
{
	GSWatcherWaylandPrivate *priv;

	priv = GS_WATCHER_WAYLAND_GET_PRIVATE (wayland);

	if (priv->idle_notifier != NULL)
	{
		ext_idle_notifier_v1_destroy (priv->idle_notifier);
		priv->idle_notifier = NULL;
	}
}

static void
on_registry_global (void                *data,
                    struct wl_registry *registry,
                    guint32             name,
                    const char         *interface,
                    guint32             version)
{
	GSWatcherWayland       *wayland = GS_WATCHER_WAYLAND (data);
	GSWatcherWaylandPrivate *priv;

	priv = GS_WATCHER_WAYLAND_GET_PRIVATE (wayland);

	if (strcmp (interface, "ext_idle_notifier_v1") == 0)
	{
		priv->idle_notifier = bind_idle_notifier (registry, name);
	}
}

static const struct wl_registry_listener registry_listener =
{
	.global = on_registry_global,
};

static void
create_idle_notification (GSWatcherWayland *wayland,
                          guint             timeout_ms)
{
	GSWatcherWaylandPrivate *priv;
	GdkDisplay              *gdk_display;
	struct wl_display       *display;
	struct wl_seat          *seat;

	priv = GS_WATCHER_WAYLAND_GET_PRIVATE (wayland);

	if (priv->idle_notifier == NULL)
	{
		gs_debug ("Wayland: idle notifier not available");
		return;
	}

	gdk_display = gdk_display_get_default ();
	display = gdk_wayland_display_get_wl_display (gdk_display);
	if (display == NULL)
	{
		gs_debug ("Wayland: could not get Wayland display");
		return;
	}

	seat = gdk_wayland_seat_get_wl_seat (gdk_display_get_default_seat (gdk_display));
	if (seat == NULL)
	{
		gs_debug ("Wayland: could not get Wayland seat");
		return;
	}

	remove_idle_notification (wayland);

	priv->idle_notification = ext_idle_notifier_v1_get_idle_notification (priv->idle_notifier,
	                                                                    timeout_ms,
	                                                                    seat);

	if (priv->idle_notification == NULL)
	{
		gs_debug ("Wayland: could not create idle notification");
		return;
	}

	ext_idle_notification_v1_add_listener (priv->idle_notification,
	                                       &idle_notification_listener,
	                                       wayland);

	gs_debug ("Wayland: idle notification created with timeout %u ms", timeout_ms);
}

static void
gs_watcher_wayland_activate_monitoring (GSWatcher *watcher,
                                        guint      timeout_ms)
{
	GSWatcherWayland       *wayland = GS_WATCHER_WAYLAND (watcher);
	GSWatcherWaylandPrivate *priv;
	struct wl_display       *display;
	struct wl_registry      *registry;

	gs_debug ("Wayland: activating idle monitoring");

	priv = GS_WATCHER_WAYLAND_GET_PRIVATE (wayland);

	display = gdk_wayland_display_get_wl_display (gdk_display_get_default ());
	if (display == NULL)
	{
		gs_debug ("Wayland: could not get Wayland display for activation");
		return;
	}

	registry = wl_display_get_registry (display);
	if (registry == NULL)
	{
		gs_debug ("Wayland: could not get Wayland registry");
		return;
	}

	wl_registry_add_listener (registry, &registry_listener, wayland);
	wl_display_roundtrip (display);

	create_idle_notification (wayland, timeout_ms);
}

static void
gs_watcher_wayland_deactivate_monitoring (GSWatcher *watcher)
{
	GSWatcherWayland *wayland = GS_WATCHER_WAYLAND (watcher);

	gs_debug ("Wayland: deactivating idle monitoring");

	remove_idle_notification (wayland);
	remove_idle_notifier (wayland);
}

static void
gs_watcher_wayland_class_init (GSWatcherWaylandClass *klass)
{
	GSWatcherClass *watcher_class = GS_WATCHER_CLASS (klass);

	watcher_class->activate_monitoring = gs_watcher_wayland_activate_monitoring;
	watcher_class->deactivate_monitoring = gs_watcher_wayland_deactivate_monitoring;
}

static void
gs_watcher_wayland_init (GSWatcherWayland *wayland)
{
	GSWatcherWaylandPrivate *priv;

	priv = GS_WATCHER_WAYLAND_GET_PRIVATE (wayland);

	priv->idle_notification = NULL;
	priv->idle_notifier = NULL;
	priv->idle_timeout_id = 0;
}

static void
gs_watcher_wayland_finalize (GObject *object)
{
	GSWatcherWayland       *wayland;
	GSWatcherWaylandPrivate *priv;

	g_return_if_fail (object != NULL);
	g_return_if_fail (GS_IS_WATCHER_WAYLAND (object));

	wayland = GS_WATCHER_WAYLAND (object);
	priv = GS_WATCHER_WAYLAND_GET_PRIVATE (wayland);

	remove_idle_notification (wayland);
	remove_idle_notifier (wayland);

	G_OBJECT_CLASS (gs_watcher_wayland_parent_class)->finalize (object);
}

GSWatcher *
gs_watcher_wayland_new (void)
{
	GSWatcher *watcher;

	watcher = g_object_new (GS_TYPE_WATCHER_WAYLAND, NULL);

	return watcher;
}
