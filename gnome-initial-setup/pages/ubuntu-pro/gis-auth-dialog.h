/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright (C) 2018 Canonical Ltd.
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * Written by:
 *     Andrea Azzarone <andrea.azzarone@canonical.com>
 */

#ifndef __GIS_AUTH_DIALOG_H__
#define __GIS_AUTH_DIALOG_H__

#define GOA_API_IS_SUBJECT_TO_CHANGE
#include <goa/goa.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GIS_TYPE_AUTH_DIALOG (gis_auth_dialog_get_type ())

G_DECLARE_FINAL_TYPE (GisAuthDialog, gis_auth_dialog, GIS, AUTH_DIALOG, GtkDialog)

GtkWidget *gis_auth_dialog_new ();
gchar     *gis_auth_dialog_get_account_id (GisAuthDialog *dialog);

G_END_DECLS

#endif /* __GIS_AUTH_DIALOG_H__ */
