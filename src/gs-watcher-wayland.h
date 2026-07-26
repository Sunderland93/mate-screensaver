/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2012-2026 MATE Developers
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __GS_WATCHER_WAYLAND_H
#define __GS_WATCHER_WAYLAND_H

#include "gs-watcher.h"

G_BEGIN_DECLS

GType gs_watcher_wayland_get_type (void);

#define GS_TYPE_WATCHER_WAYLAND (gs_watcher_wayland_get_type ())

G_END_DECLS

#endif /* __GS_WATCHER_WAYLAND_H */
