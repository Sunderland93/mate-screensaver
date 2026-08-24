/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2012-2026 MATE Developers
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __GS_WINDOW_PRIVATE_H
#define __GS_WINDOW_PRIVATE_H

#include "gs-window.h"

G_BEGIN_DECLS

enum
{
	GS_WINDOW_SIGNAL_ACTIVITY = 0,
	GS_WINDOW_SIGNAL_DEACTIVATED,
	GS_WINDOW_SIGNAL_DIALOG_UP,
	GS_WINDOW_SIGNAL_DIALOG_DOWN,
	GS_WINDOW_N_SIGNALS
};

extern guint gs_window_signals [GS_WINDOW_N_SIGNALS];

struct GSWindowPrivate
{
	GdkMonitor    *monitor;
	gboolean       lock_enabled;
	gboolean       logout_enabled;
	gboolean       keyboard_enabled;
	gboolean       user_switch_enabled;
	char          *keyboard_command;
	char          *logout_command;
	glong          logout_timeout;
	char          *status_message;

	guint      obscured : 1;
	guint      dialog_up : 1;

	/* The widget where saver themes get embedded; on Wayland this is a
	   WleGtkSocket, on X11 a GtkSocket */
	GtkWidget *drawing_area;

#ifdef ENABLE_X11
	GdkRectangle geometry;

	GtkWidget *vbox;
	GtkWidget *lock_box;
	GtkWidget *lock_socket;
	GtkWidget *keyboard_socket;
	GtkWidget *info_bar;
	GtkWidget *info_content;

	cairo_surface_t *background_surface;

	guint      popup_dialog_idle_id;

	guint      dialog_map_signal_id;
	guint      dialog_unmap_signal_id;
	guint      dialog_response_signal_id;

	guint      watchdog_timer_id;
	guint      info_bar_timer_id;

	gint       lock_pid;
	gint       lock_watch_id;
	gint       dialog_response;
	gboolean   dialog_quit_requested;
	gboolean   dialog_shake_in_progress;

	gint       keyboard_pid;
	gint       keyboard_watch_id;

	GList     *key_events;

	gdouble    last_x;
	gdouble    last_y;

	GTimer    *timer;

#ifdef HAVE_SHAPE_EXT
	int        shape_event_base;
#endif
#endif /* ENABLE_X11 */
};

G_END_DECLS

#endif /* __GS_WINDOW_PRIVATE_H */
