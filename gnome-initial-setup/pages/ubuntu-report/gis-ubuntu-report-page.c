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
 */

/* Ubuntu report page {{{1 */

#define PAGE_ID "ubuntu-report"

#include "config.h"
#include "gis-ubuntu-report-page.h"
#include "ubuntu-report-resources.h"

#include <glib/gi18n.h>
#include <gio/gio.h>
#include <libsysmetrics.h>

struct _GisUbuntuReportPagePrivate {
  GtkWidget *description_label;
  GtkWidget *opt_in_radio;
  gchar *report;
};
typedef struct _GisUbuntuReportPagePrivate GisUbuntuReportPagePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (GisUbuntuReportPage, gis_ubuntu_report_page, GIS_TYPE_PAGE);

static void
gis_ubuntu_report_page_constructed (GObject *object)
{
  GisUbuntuReportPage *page = GIS_UBUNTU_REPORT_PAGE (object);

  G_OBJECT_CLASS (gis_ubuntu_report_page_parent_class)->constructed (object);

  gis_page_set_skippable (GIS_PAGE (page), TRUE);

  gis_page_set_complete (GIS_PAGE (page), TRUE);
  gtk_widget_show (GTK_WIDGET (page));
}

static void
gis_ubuntu_report_page_finalize (GObject *object)
{
  GisUbuntuReportPage *page = GIS_UBUNTU_REPORT_PAGE (object);
  GisUbuntuReportPagePrivate *priv = gis_ubuntu_report_page_get_instance_private (page);

  g_free (priv->report);

  G_OBJECT_CLASS (gis_ubuntu_report_page_parent_class)->finalize (object);
}

static void
show_report (GtkButton *button, GisUbuntuReportPage *page)
{
  GisUbuntuReportPagePrivate *priv = gis_ubuntu_report_page_get_instance_private (page);
  g_autofree char *error = NULL;
  GtkWidget *dialog, *scroll, *text_view;

  if (priv->report == NULL)
    error = sysmetrics_collect (&priv->report);
  if (error != NULL) {
    dialog = gtk_message_dialog_new (GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (page))),
                                     GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                     GTK_MESSAGE_ERROR,
                                     GTK_BUTTONS_CLOSE,
                                     _("Failed to get report information: %s"), error);
    gtk_dialog_run (GTK_DIALOG (dialog));
    gtk_widget_destroy (dialog);
    return;
  }

  dialog = gtk_dialog_new_with_buttons (_("Report"),
                                        GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (page))),
                                        GTK_DIALOG_MODAL |
                                        GTK_DIALOG_DESTROY_WITH_PARENT |
                                        GTK_DIALOG_USE_HEADER_BAR,
                                       NULL, NULL);
  gtk_widget_set_size_request (dialog, 800, 600);

  scroll = gtk_scrolled_window_new (NULL, NULL);
  gtk_widget_show (scroll);
  gtk_box_pack_start (GTK_BOX (gtk_dialog_get_content_area (GTK_DIALOG (dialog))), scroll, TRUE, TRUE, 0);

  text_view = gtk_text_view_new ();
  gtk_widget_show (text_view);
  gtk_text_buffer_set_text (gtk_text_view_get_buffer (GTK_TEXT_VIEW (text_view)), priv->report, -1);
  gtk_container_add (GTK_CONTAINER (scroll), text_view);

  gtk_dialog_run (GTK_DIALOG (dialog));
  gtk_widget_destroy (dialog);
}

static void
show_legal (GtkButton *button, GisUbuntuReportPage *page)
{
  g_autoptr(GError) error = NULL;

  if (!gtk_show_uri_on_window (GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (page))),
                               "https://www.ubuntu.com/legal/terms-and-policies/systems-information-notice",
                               GDK_CURRENT_TIME, &error)) {
    GtkWidget *dialog;
    dialog = gtk_message_dialog_new (GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (page))),
                                     GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                     GTK_MESSAGE_ERROR,
                                     GTK_BUTTONS_CLOSE,
                                     _("Failed to show privacy policy: %s"), error->message);
    gtk_dialog_run (GTK_DIALOG (dialog));
    gtk_widget_destroy (dialog);
    return;
  }
}

static gboolean
gis_ubuntu_report_page_apply (GisPage *page, GCancellable *cancellable)
{
  GisUbuntuReportPagePrivate *priv = gis_ubuntu_report_page_get_instance_private (GIS_UBUNTU_REPORT_PAGE (page));
  g_autofree char *error = NULL;

  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (priv->opt_in_radio))) {
    if (priv->report == NULL) {
      error = sysmetrics_collect (&priv->report);
      if (error != NULL) {
        g_warning ("Failed to get report data: %s", error);
        return FALSE;
      }
    }

    error = sysmetrics_send_report (priv->report, FALSE, "");
    if (error != NULL)
      g_warning ("Failed to send report: %s", error);
  } else {
    error = sysmetrics_send_decline (FALSE, "");
    if (error != NULL)
      g_warning ("Failed to send decline: %s", error);
  }

  return FALSE;
}

static void
gis_ubuntu_report_page_locale_changed (GisPage *page)
{
  GisUbuntuReportPagePrivate *priv = gis_ubuntu_report_page_get_instance_private (GIS_UBUNTU_REPORT_PAGE (page));
  g_autofree gchar *timezone_text = NULL;
  g_autofree gchar *description_text = NULL;
  g_autoptr(GError) error = NULL;

  gis_page_set_title (GIS_PAGE (page), _("Help improve Ubuntu"));

  if (!g_file_get_contents ("/etc/timezone", &timezone_text, NULL, &error)) {
    g_warning ("Failed to get timezone from /etc/timezone: %s", error->message);
    return;
  }

  description_text = g_strdup_printf (_("Ubuntu can report information that helps developers improve it. "
                                        "This includes things like the computer model, "
					"what software is installed, "
					"and the approximate location you chose (%s)."), g_strstrip (timezone_text));
  gtk_label_set_label (GTK_LABEL (priv->description_label), description_text);
}

static void
gis_ubuntu_report_page_class_init (GisUbuntuReportPageClass *klass)
{
  GisPageClass *page_class = GIS_PAGE_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  gtk_widget_class_set_template_from_resource (GTK_WIDGET_CLASS (klass), "/org/gnome/initial-setup/gis-ubuntu-report-page.ui");
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisUbuntuReportPage, description_label);
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisUbuntuReportPage, opt_in_radio);
  gtk_widget_class_bind_template_callback (GTK_WIDGET_CLASS (klass), show_report);
  gtk_widget_class_bind_template_callback (GTK_WIDGET_CLASS (klass), show_legal);

  page_class->page_id = PAGE_ID;
  page_class->apply = gis_ubuntu_report_page_apply;
  page_class->locale_changed = gis_ubuntu_report_page_locale_changed;
  object_class->constructed = gis_ubuntu_report_page_constructed;
  object_class->finalize = gis_ubuntu_report_page_finalize;
}

static void
gis_ubuntu_report_page_init (GisUbuntuReportPage *page)
{
  g_resources_register (ubuntu_report_get_resource ());

  gtk_widget_init_template (GTK_WIDGET (page));
}

GisPage *
gis_prepare_ubuntu_report_page (GisDriver *driver)
{
  return g_object_new (GIS_TYPE_UBUNTU_REPORT_PAGE,
                       "driver", driver,
                       NULL);
}
