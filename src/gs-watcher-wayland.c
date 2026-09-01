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
	struct ext_idle_notification_v1  *lock_notification;
	struct ext_idle_notifier_v1      *idle_notifier;
	struct wl_registry              *registry;
	guint                            timeout_ms;
	guint                            lock_delay_ms;
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

static void remove_idle_notification (GSWatcherWayland *wayland,
                                      struct ext_idle_notification_v1 **notification);

static void
on_activation_idled (void                            *data,
                     struct ext_idle_notification_v1 *notification)
{
	GSWatcherWayland *wayland = GS_WATCHER_WAYLAND (data);
	GSWatcher        *watcher = GS_WATCHER (wayland);

	gs_debug ("Wayland: activation idle notification fired");

	_gs_watcher_set_session_idle_notice (watcher, FALSE);
	_gs_watcher_set_session_idle (watcher, TRUE);
}

static void
on_activation_resumed (void                            *data,
                       struct ext_idle_notification_v1 *notification)
{
	GSWatcherWayland *wayland = GS_WATCHER_WAYLAND (data);
	GSWatcher        *watcher = GS_WATCHER (wayland);

	gs_debug ("Wayland: activation resumed");

	_gs_watcher_set_session_idle (watcher, FALSE);
	_gs_watcher_set_session_idle_notice (watcher, FALSE);
}

static struct ext_idle_notification_v1_listener activation_listener =
{
	.idled = on_activation_idled,
	.resumed = on_activation_resumed,
};

static void
on_lock_notice_idled (void                            *data,
                      struct ext_idle_notification_v1 *notification)
{
	GSWatcherWayland *wayland = GS_WATCHER_WAYLAND (data);
	GSWatcher        *watcher = GS_WATCHER (wayland);

	gs_debug ("Wayland: lock notice idle notification fired");

	_gs_watcher_set_session_idle_notice (watcher, TRUE);
}

static void
on_lock_notice_resumed (void                            *data,
                        struct ext_idle_notification_v1 *notification)
{
	GSWatcherWayland *wayland = GS_WATCHER_WAYLAND (data);
	GSWatcher        *watcher = GS_WATCHER (wayland);

	gs_debug ("Wayland: lock notice resumed");

	_gs_watcher_set_session_idle (watcher, FALSE);
	_gs_watcher_set_session_idle_notice (watcher, FALSE);
}

static struct ext_idle_notification_v1_listener lock_notice_listener =
{
	.idled = on_lock_notice_idled,
	.resumed = on_lock_notice_resumed,
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
remove_idle_notification (GSWatcherWayland                  *wayland,
                          struct ext_idle_notification_v1 **notification)
{
	if (*notification != NULL)
	{
		ext_idle_notification_v1_destroy (*notification);
		*notification = NULL;
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
remove_registry (GSWatcherWayland *wayland)
{
	GSWatcherWaylandPrivate *priv;

	priv = GS_WATCHER_WAYLAND_GET_PRIVATE (wayland);

	if (priv->registry != NULL)
	{
		wl_registry_destroy (priv->registry);
		priv->registry = NULL;
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

static struct ext_idle_notification_v1 *
create_idle_notification (GSWatcherWayland *wayland,
                          guint             timeout_ms,
                          struct ext_idle_notification_v1_listener *listener)
{
	GSWatcherWaylandPrivate *priv;
	GdkDisplay              *gdk_display;
	struct wl_display       *display;
	struct wl_seat          *seat;
	struct ext_idle_notification_v1 *notification;

	priv = GS_WATCHER_WAYLAND_GET_PRIVATE (wayland);

	if (priv->idle_notifier == NULL)
	{
		gs_debug ("Wayland: idle notifier not available");
		return NULL;
	}

	gdk_display = gdk_display_get_default ();
	display = gdk_wayland_display_get_wl_display (gdk_display);
	if (display == NULL)
	{
		gs_debug ("Wayland: could not get Wayland display");
		return NULL;
	}

	seat = gdk_wayland_seat_get_wl_seat (gdk_display_get_default_seat (gdk_display));
	if (seat == NULL)
	{
		gs_debug ("Wayland: could not get Wayland seat");
		return NULL;
	}

	notification = ext_idle_notifier_v1_get_idle_notification (priv->idle_notifier,
	                                                           timeout_ms,
	                                                           seat);

	if (notification == NULL)
	{
		gs_debug ("Wayland: could not create idle notification for %u ms", timeout_ms);
		return NULL;
	}

	ext_idle_notification_v1_add_listener (notification,
	                                       listener,
	                                       wayland);

	gs_debug ("Wayland: idle notification created with timeout %u ms", timeout_ms);

	return notification;
}

static void
gs_watcher_wayland_activate_monitoring (GSWatcher *watcher,
                                        guint      timeout_ms)
{
	GSWatcherWayland       *wayland = GS_WATCHER_WAYLAND (watcher);
	GSWatcherWaylandPrivate *priv;
	struct wl_display       *display;

	gs_debug ("Wayland: activating idle monitoring");

	priv = GS_WATCHER_WAYLAND_GET_PRIVATE (wayland);

	priv->timeout_ms = timeout_ms;

	display = gdk_wayland_display_get_wl_display (gdk_display_get_default ());
	if (display == NULL)
	{
		gs_debug ("Wayland: could not get Wayland display for activation");
		return;
	}

	priv->registry = wl_display_get_registry (display);
	if (priv->registry == NULL)
	{
		gs_debug ("Wayland: could not get Wayland registry");
		return;
	}

	wl_registry_add_listener (priv->registry, &registry_listener, wayland);
	wl_display_roundtrip (display);

	remove_idle_notification (wayland, &priv->idle_notification);
	remove_idle_notification (wayland, &priv->lock_notification);

	priv->idle_notification = create_idle_notification (wayland,
	                                                    timeout_ms,
	                                                    &activation_listener);

	if (priv->lock_delay_ms > 0)
	{
		priv->lock_notification = create_idle_notification (wayland,
		                                                    timeout_ms + priv->lock_delay_ms,
		                                                    &lock_notice_listener);
	}
}

static void
gs_watcher_wayland_deactivate_monitoring (GSWatcher *watcher)
{
	GSWatcherWayland       *wayland = GS_WATCHER_WAYLAND (watcher);
	GSWatcherWaylandPrivate *priv;

	gs_debug ("Wayland: deactivating idle monitoring");

	priv = GS_WATCHER_WAYLAND_GET_PRIVATE (wayland);

	remove_idle_notification (wayland, &priv->idle_notification);
	remove_idle_notification (wayland, &priv->lock_notification);
	remove_idle_notifier (wayland);
	remove_registry (wayland);
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
	priv->lock_notification = NULL;
	priv->idle_notifier = NULL;
	priv->registry = NULL;
	priv->timeout_ms = 0;
	priv->lock_delay_ms = 0;
}

static void
gs_watcher_wayland_finalize (GObject *object)
{
	GSWatcherWayland        *wayland;
	GSWatcherWaylandPrivate *priv;

	g_return_if_fail (object != NULL);
	g_return_if_fail (GS_IS_WATCHER_WAYLAND (object));

	wayland = GS_WATCHER_WAYLAND (object);
	priv = GS_WATCHER_WAYLAND_GET_PRIVATE (wayland);

	remove_idle_notification (wayland, &priv->idle_notification);
	remove_idle_notification (wayland, &priv->lock_notification);
	remove_idle_notifier (wayland);
	remove_registry (wayland);

	G_OBJECT_CLASS (gs_watcher_wayland_parent_class)->finalize (object);
}

GSWatcher *
gs_watcher_wayland_new (void)
{
	GSWatcher *watcher;

	watcher = g_object_new (GS_TYPE_WATCHER_WAYLAND, NULL);

	return watcher;
}
