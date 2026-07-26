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

#include <gdk/gdk.h>

#include "gs-grab.h"
#include "gs-debug.h"

#ifdef ENABLE_WAYLAND
#include <gdk/gdkwayland.h>
extern GType gs_grab_wayland_get_type (void);
#endif
#ifdef ENABLE_X11
#include <gdk/gdkx.h>
extern GType gs_grab_x11_get_type (void);
#endif

G_DEFINE_ABSTRACT_TYPE (GSGrab, gs_grab, G_TYPE_OBJECT)

static void
gs_grab_class_init (GSGrabClass *klass)
{
}

static void
gs_grab_init (GSGrab *grab)
{
}

GSGrab *
gs_grab_new (void)
{
	GdkDisplay *display;
	GSGrab     *grab = NULL;

	display = gdk_display_get_default ();

#ifdef ENABLE_WAYLAND
	if (GDK_IS_WAYLAND_DISPLAY (display))
	{
		grab = g_object_new (gs_grab_wayland_get_type (), NULL);
	}
	else
#endif
#ifdef ENABLE_X11
	if (GDK_IS_X11_DISPLAY (display))
	{
		grab = g_object_new (gs_grab_x11_get_type (), NULL);
	}
	else
#endif
	{
		gs_debug ("No grab handler available for this display");
		return NULL;
	}

	return grab;
}

void
gs_grab_release (GSGrab  *grab,
                 gboolean flush)
{
	g_return_if_fail (GS_IS_GRAB (grab));

	if (GS_GRAB_GET_CLASS (grab)->release == NULL)
	{
		g_warning ("GSGrab does not implement release");
		return;
	}

	GS_GRAB_GET_CLASS (grab)->release (grab, flush);
}

gboolean
gs_grab_grab_window (GSGrab     *grab,
                     GdkWindow  *window,
                     GdkDisplay *display,
                     gboolean    no_pointer_grab,
                     gboolean    hide_cursor)
{
	g_return_val_if_fail (GS_IS_GRAB (grab), FALSE);

	if (GS_GRAB_GET_CLASS (grab)->grab_window == NULL)
	{
		g_warning ("GSGrab does not implement grab_window");
		return FALSE;
	}

	return GS_GRAB_GET_CLASS (grab)->grab_window (grab, window, display,
	                                               no_pointer_grab,
	                                               hide_cursor);
}

gboolean
gs_grab_grab_root (GSGrab  *grab,
                   gboolean no_pointer_grab,
                   gboolean hide_cursor)
{
	g_return_val_if_fail (GS_IS_GRAB (grab), FALSE);

	if (GS_GRAB_GET_CLASS (grab)->grab_root == NULL)
	{
		g_warning ("GSGrab does not implement grab_root");
		return FALSE;
	}

	return GS_GRAB_GET_CLASS (grab)->grab_root (grab, no_pointer_grab,
	                                             hide_cursor);
}

gboolean
gs_grab_grab_offscreen (GSGrab  *grab,
                        gboolean no_pointer_grab,
                        gboolean hide_cursor)
{
	g_return_val_if_fail (GS_IS_GRAB (grab), FALSE);

	if (GS_GRAB_GET_CLASS (grab)->grab_offscreen == NULL)
	{
		g_warning ("GSGrab does not implement grab_offscreen");
		return FALSE;
	}

	return GS_GRAB_GET_CLASS (grab)->grab_offscreen (grab, no_pointer_grab,
	                                                  hide_cursor);
}

void
gs_grab_move_to_window (GSGrab     *grab,
                        GdkWindow  *window,
                        GdkDisplay *display,
                        gboolean    no_pointer_grab,
                        gboolean    hide_cursor)
{
	g_return_if_fail (GS_IS_GRAB (grab));

	if (GS_GRAB_GET_CLASS (grab)->move_to_window == NULL)
	{
		g_warning ("GSGrab does not implement move_to_window");
		return;
	}

	GS_GRAB_GET_CLASS (grab)->move_to_window (grab, window, display,
	                                           no_pointer_grab,
	                                           hide_cursor);
}

void
gs_grab_reset (GSGrab *grab)
{
	g_return_if_fail (GS_IS_GRAB (grab));

	if (GS_GRAB_GET_CLASS (grab)->reset == NULL)
	{
		g_warning ("GSGrab does not implement reset");
		return;
	}

	GS_GRAB_GET_CLASS (grab)->reset (grab);
}
