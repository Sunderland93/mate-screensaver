/*
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
 */

#ifndef __GS_WATCHER_PRIVATE_H
#define __GS_WATCHER_PRIVATE_H

#include "gs-watcher.h"

typedef enum
{
        GS_WATCHER_SIGNAL_IDLE_CHANGED = 0,
        GS_WATCHER_SIGNAL_IDLE_NOTICE_CHANGED,
        GS_WATCHER_N_SIGNALS
} GSWatcherSignals;

extern guint gs_watcher_signals [GS_WATCHER_N_SIGNALS];

struct GSWatcherPrivate
{
    gboolean enabled;
    gboolean active;
    gboolean idle_notice;
    gboolean idle;
    char *status_message;
};

gboolean _gs_watcher_set_session_idle     (GSWatcher *watcher,
                                           gboolean   is_idle);
gboolean _gs_watcher_set_session_idle_notice (GSWatcher *watcher,
                                              gboolean   in_effect);

#endif /* __GS_WATCHER_PRIVATE_H */
