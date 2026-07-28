/*
 * gs-watcher.c
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * Authors:
 *     Marcus Johnson <marcusl@littlesvr.ca>
 */

#include "config.h"
#include "gs-watcher.h"
#include "gs-watcher-private.h"

#ifdef ENABLE_WAYLAND
#include <gdk/gdkwayland.h>
#include "gs-watcher-wayland.h"
#endif

#ifdef ENABLE_X11
#include <gdk/gdkx.h>
#include "gs-watcher-x11.h"
#endif

#define GS_WATCHER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GS_TYPE_WATCHER, GSWatcherPrivate))

enum
{
    PROP_0,
    PROP_ENABLED,
    PROP_ACTIVE,
    PROP_STATUS_MESSAGE,
};

static void gs_watcher_set_property (GObject      *object,
                                     guint         prop_id,
                                     const GValue *value,
                                     GParamSpec   *pspec);
static void gs_watcher_get_property (GObject    *object,
                                     guint       prop_id,
                                     GValue     *value,
                                     GParamSpec *pspec);
static void gs_watcher_finalize (GObject *object);

static gboolean gs_boolean_accumulator (GSignalInvocationHint *ihint,
                                        GValue                *return_accu,
                                        const GValue          *handler_return,
                                        gpointer               dummy);

G_DEFINE_ABSTRACT_TYPE (GSWatcher, gs_watcher, G_TYPE_OBJECT)

static guint signals[GS_WATCHER_N_SIGNALS] = { 0, };

static void
gs_watcher_class_init (GSWatcherClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->set_property = gs_watcher_set_property;
    object_class->get_property = gs_watcher_get_property;
    object_class->finalize = gs_watcher_finalize;

    klass->idle_changed = NULL;
    klass->idle_notice_changed = NULL;
    klass->activate_monitoring = NULL;
    klass->deactivate_monitoring = NULL;

    g_object_class_install_property (object_class,
                                     PROP_ENABLED,
                                     g_param_spec_boolean ("enabled",
                                                           NULL,
                                                           NULL,
                                                           TRUE,
                                                           G_PARAM_READWRITE |
                                                           G_PARAM_STATIC_STRINGS));

    g_object_class_install_property (object_class,
                                     PROP_ACTIVE,
                                     g_param_spec_boolean ("active",
                                                           NULL,
                                                           NULL,
                                                           FALSE,
                                                           G_PARAM_READWRITE |
                                                           G_PARAM_STATIC_STRINGS));

    g_object_class_install_property (object_class,
                                     PROP_STATUS_MESSAGE,
                                     g_param_spec_string ("status-message",
                                                          NULL,
                                                          NULL,
                                                          NULL,
                                                          G_PARAM_READABLE |
                                                          G_PARAM_STATIC_STRINGS));

    signals[GS_WATCHER_SIGNAL_IDLE_CHANGED] =
        g_signal_new ("idle-changed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      gs_boolean_accumulator, NULL,
                      NULL,
                      G_TYPE_BOOLEAN, 1,
                      G_TYPE_BOOLEAN);

    signals[GS_WATCHER_SIGNAL_IDLE_NOTICE_CHANGED] =
        g_signal_new ("idle-notice-changed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      gs_boolean_accumulator, NULL,
                      NULL,
                      G_TYPE_BOOLEAN, 1,
                      G_TYPE_BOOLEAN);

    g_type_class_add_private (klass, sizeof (GSWatcherPrivate));
}

static void
gs_watcher_init (GSWatcher *watcher)
{
    GSWatcherPrivate *priv;

    priv = GS_WATCHER_GET_PRIVATE (watcher);
    watcher->priv = priv;

    priv->enabled = TRUE;
    priv->active = FALSE;
    priv->idle_notice = FALSE;
    priv->idle = FALSE;
    priv->status_message = NULL;
}

static gboolean
gs_boolean_accumulator (GSignalInvocationHint *ihint,
                        GValue                *return_accu,
                        const GValue          *handler_return,
                        gpointer               dummy)
{
    gboolean continue_emission;
    gboolean signal_return;

    signal_return = g_value_get_boolean (return_accu);
    continue_emission = !g_value_get_boolean (handler_return);

    g_value_set_boolean (return_accu, signal_return || g_value_get_boolean (handler_return));

    return continue_emission;
}

gboolean
_gs_watcher_set_session_idle (GSWatcher *watcher,
                              gboolean   is_idle)
{
    GSWatcherPrivate *priv;

    g_return_val_if_fail (GS_IS_WATCHER (watcher), FALSE);

    priv = watcher->priv;

    if (priv->idle == is_idle)
        return TRUE;

    priv->idle = is_idle;

    g_signal_emit (watcher, signals[GS_WATCHER_SIGNAL_IDLE_CHANGED], 0,
                   is_idle, NULL);

    return TRUE;
}

gboolean
_gs_watcher_set_session_idle_notice (GSWatcher *watcher,
                                     gboolean   in_effect)
{
    GSWatcherPrivate *priv;

    g_return_val_if_fail (GS_IS_WATCHER (watcher), FALSE);

    priv = watcher->priv;

    if (priv->idle_notice == in_effect)
        return TRUE;

    priv->idle_notice = in_effect;

    g_signal_emit (watcher, signals[GS_WATCHER_SIGNAL_IDLE_NOTICE_CHANGED], 0,
                   in_effect, NULL);

    return TRUE;
}

gboolean
gs_watcher_set_enabled (GSWatcher *watcher,
                        gboolean   enabled)
{
    GSWatcherPrivate *priv;

    g_return_val_if_fail (GS_IS_WATCHER (watcher), FALSE);

    priv = watcher->priv;

    if (priv->enabled == enabled)
        return TRUE;

    priv->enabled = enabled;

    g_object_notify (G_OBJECT (watcher), "enabled");

    return TRUE;
}

gboolean
gs_watcher_get_enabled (GSWatcher *watcher)
{
    g_return_val_if_fail (GS_IS_WATCHER (watcher), FALSE);

    return watcher->priv->enabled;
}

gboolean
gs_watcher_set_active (GSWatcher *watcher,
                       gboolean   active)
{
    GSWatcherPrivate *priv;

    g_return_val_if_fail (GS_IS_WATCHER (watcher), FALSE);

    priv = watcher->priv;

    if (priv->active == active)
        return TRUE;

    priv->active = active;

    if (active)
    {
        if (GS_WATCHER_GET_CLASS (watcher)->activate_monitoring)
            GS_WATCHER_GET_CLASS (watcher)->activate_monitoring (watcher, priv->idle_timeout_ms);
    }
    else
    {
        if (GS_WATCHER_GET_CLASS (watcher)->deactivate_monitoring)
            GS_WATCHER_GET_CLASS (watcher)->deactivate_monitoring (watcher);
    }

    g_object_notify (G_OBJECT (watcher), "active");

    return TRUE;
}

gboolean
gs_watcher_get_active (GSWatcher *watcher)
{
    g_return_val_if_fail (GS_IS_WATCHER (watcher), FALSE);

    return watcher->priv->active;
}

void
gs_watcher_set_idle_timeout (GSWatcher *watcher,
                             guint      timeout_ms)
{
    g_return_if_fail (GS_IS_WATCHER (watcher));

    watcher->priv->idle_timeout_ms = timeout_ms;
}

GSWatcher *
gs_watcher_new (void)
{
    GSWatcher *watcher = NULL;

#ifdef ENABLE_WAYLAND
    if (GDK_IS_WAYLAND_DISPLAY (gdk_display_get_default ()))
    {
        watcher = g_object_new (GS_TYPE_WATCHER_WAYLAND, NULL);
        return watcher;
    }
#endif

#ifdef ENABLE_X11
    if (GDK_IS_X11_DISPLAY (gdk_display_get_default ()))
    {
        watcher = g_object_new (GS_TYPE_WATCHER_X11, NULL);
        return watcher;
    }
#endif

    return NULL;
}

static void
gs_watcher_set_property (GObject      *object,
                         guint         prop_id,
                         const GValue *value,
                         GParamSpec   *pspec)
{
    GSWatcher *watcher = GS_WATCHER (object);

    switch (prop_id)
    {
    case PROP_ENABLED:
        gs_watcher_set_enabled (watcher, g_value_get_boolean (value));
        break;
    case PROP_ACTIVE:
        gs_watcher_set_active (watcher, g_value_get_boolean (value));
        break;
    case PROP_STATUS_MESSAGE:
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
gs_watcher_get_property (GObject    *object,
                         guint       prop_id,
                         GValue     *value,
                         GParamSpec *pspec)
{
    GSWatcher *watcher = GS_WATCHER (object);
    GSWatcherPrivate *priv = watcher->priv;

    switch (prop_id)
    {
    case PROP_ENABLED:
        g_value_set_boolean (value, priv->enabled);
        break;
    case PROP_ACTIVE:
        g_value_set_boolean (value, priv->active);
        break;
    case PROP_STATUS_MESSAGE:
        g_value_set_string (value, priv->status_message);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
gs_watcher_finalize (GObject *object)
{
    GSWatcher *watcher = GS_WATCHER (object);
    GSWatcherPrivate *priv = watcher->priv;

    g_free (priv->status_message);
    priv->status_message = NULL;

    G_OBJECT_CLASS (gs_watcher_parent_class)->finalize (object);
}
