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

#ifndef __GS_WINDOW_WAYLAND_H
#define __GS_WINDOW_WAYLAND_H

#include <gdk/gdk.h>
#include <gtk/gtk.h>

#include "gs-window.h"

G_BEGIN_DECLS

GType       gs_window_wayland_get_type   (void) G_GNUC_CONST;

GSWindow *  gs_window_wayland_new        (GdkMonitor *monitor,
                                          gboolean    lock_enabled);

G_END_DECLS

#endif /* __GS_WINDOW_WAYLAND_H */
