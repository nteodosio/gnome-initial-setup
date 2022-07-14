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

/* Canonical Livepatch page {{{1 */

#define PAGE_ID "livepatch"

#include "config.h"
#include "gis-auth-dialog.h"
#include "gis-livepatch-page.h"
#include "livepatch-resources.h"

#define GOA_API_IS_SUBJECT_TO_CHANGE
#include <goa/goa.h>
#define GOA_BACKEND_API_IS_SUBJECT_TO_CHANGE
#include <goabackend/goabackend.h>

#include <glib/gi18n.h>
#include <gio/gio.h>
#include <polkit/polkit.h>

struct _GisLivepatchPagePrivate {
  GtkWidget *setup_button;
  GtkWidget *message_box;
  GtkWidget *signout_button;
  GtkWidget *message_label;

  GoaClient *goa_client;
  GoaAccount *goa_account;
  GPermission *permission;

  gchar *token;
  gboolean waiting_for_livepatch_response;
  gboolean user_current_choice;
};
typedef struct _GisLivepatchPagePrivate GisLivepatchPagePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (GisLivepatchPage, gis_livepatch_page, GIS_TYPE_PAGE);

#define LIVEPATCH_ENABLING_MESSAGE  _("You're all set: Livepatch is now being enabled.")
#define LIVEPATCH_ENABLE_SUCCESS_MESSAGE  _("You're all set: Livepatch is now on.")
#define LIVEPATCH_ENABLE_FAILURE_MESSAGE  _("Failed to setup Livepatch: please retry.")
#define LIVEPATCH_DISABLE_FAILURE_MESSAGE _("Failed to disable Livepatch: please retry.")

static gboolean
set_livepatch_enabled (GisLivepatchPage *page,
                       gboolean          value);

static gboolean
is_livepatch_enabled ()
{
  return g_file_test ("/var/snap/canonical-livepatch/common/machine-token",
                      G_FILE_TEST_EXISTS);
}

static char *
get_item (const char *buffer, const char *name)
{
  g_autofree gchar *label = NULL;
  gchar *start = NULL, *end = NULL;
  gchar end_char;

  label = g_strconcat (name, "=", NULL);
  if ((start = strstr (buffer, label)) != NULL) {
    start += strlen (label);
    end_char = '\n';
    if (*start == '"') {
      start++;
      end_char = '"';
    }
    end = strchr (start, end_char);
  }

  if (start != NULL && end != NULL)
    return g_strndup (start, end - start);
  else
    return NULL;
}

static gboolean
is_lts ()
{
  g_autofree gchar *buffer = NULL;
  g_autofree gchar *version = NULL;

  if (g_file_get_contents ("/etc/os-release", &buffer, NULL, NULL))
    version = get_item (buffer, "VERSION");

  return version && g_strrstr (version, "LTS") != NULL;
}

static gboolean
is_livepatch_supported ()
{
  return is_lts ();
}

static void
open_software_properties ()
{
  g_autofree gchar *command = NULL;
  g_autoptr(GAppInfo) info = NULL;
  g_autoptr(GError) error = NULL;

  info = g_app_info_create_from_commandline ("software-properties-gtk --open-tab=2", NULL, G_APP_INFO_CREATE_NONE, &error);
  if (info == NULL) {
     g_warning ("Failed to get launch information from software-properties-gtk: %s", error->message);
     return;
  }

  if (!g_app_info_launch (info, NULL, NULL, &error)) {
     g_warning ("Failed to launch software-properties-gtk: %s", error->message);
     return;
  }
}

static void
on_livepatch_enabled (GObject *source_object,
                      GAsyncResult *res,
                      gpointer data)
{
  GisLivepatchPage *page = GIS_LIVEPATCH_PAGE (data);
  GisLivepatchPagePrivate *priv = gis_livepatch_page_get_instance_private (page);
  g_autoptr(GVariant) result = NULL;
  gboolean success = TRUE;
  gboolean current_state = is_livepatch_enabled ();
  g_autofree gchar *out_message = NULL;
  g_autoptr(GError) error = NULL;

  priv->waiting_for_livepatch_response = FALSE;

  result = g_dbus_proxy_call_finish (G_DBUS_PROXY (source_object), res, &error);
  if (result == NULL) {
    g_warning ("Failed to enable/disable Livepatch through DBus: %s\n", error->message);
    out_message = g_strdup (error->message);
    success = FALSE;
  } else {
    gboolean out_error;

    g_variant_get (result, "(bs)", &out_error, &out_message);
    if (out_error) {
      g_warning ("Failed to enable/disable Livepatch: %s\n", out_message);
      success = FALSE;
    }
  }

  if (success) {
     /* We succeded to enable or disable livepatch.
        Check if this corresponds to the current user choice. */
    if (current_state != priv->user_current_choice) {
      set_livepatch_enabled (page, priv->user_current_choice);
    } else if (current_state) {
      gtk_label_set_text (GTK_LABEL (priv->message_label), LIVEPATCH_ENABLE_SUCCESS_MESSAGE);
    }
  } else if (current_state != priv->user_current_choice) {
    GisAssistant *assistant = gis_driver_get_assistant (GIS_PAGE (page)->driver);

    /* We failed to enable or disable livepatch. Show an error message if
       the current state does not correpond the current user choice.
       Ignore the message if the call failed but the current status correponds
       to the user choice. */
    if (priv->user_current_choice) {
      gtk_label_set_text (GTK_LABEL (priv->message_label), LIVEPATCH_ENABLE_FAILURE_MESSAGE);
      gtk_widget_set_sensitive (priv->setup_button, TRUE);
    } else {
      gtk_label_set_text (GTK_LABEL (priv->message_label), LIVEPATCH_DISABLE_FAILURE_MESSAGE);
    }

    if (gis_assistant_get_current_page (assistant) != GIS_PAGE (page)) {
      GtkWidget *dialog;

      dialog =  gtk_message_dialog_new_with_markup (GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (page))),
                                                    GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                    GTK_MESSAGE_ERROR,
                                                    GTK_BUTTONS_NONE,
                                                    "<span font_size='x-large' font_weight='bold'>%s</span>",
                                                    _("Sorry there's been a problem with setting up Canonical Livepatch"));
      gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
                                                _("The error was: %s"),
                                                out_message);
      gtk_dialog_add_buttons (GTK_DIALOG (dialog),
                             _("Settings…"),
                             GTK_RESPONSE_OK,
                             _("Ignore"),
                             GTK_RESPONSE_CANCEL,
                             NULL);

      if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_OK)
        open_software_properties ();

      gtk_widget_destroy (dialog);
    }
  }

  gis_driver_uninhibit_quit (GIS_PAGE (page)->driver);
}

static gboolean
set_livepatch_enabled (GisLivepatchPage *page,
                       gboolean          value)
{
  GisLivepatchPagePrivate *priv = gis_livepatch_page_get_instance_private (page);
  g_autoptr(GDBusProxy) proxy = NULL;
  g_autoptr(GError) error = NULL;
  GVariant *args;

  proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                         G_DBUS_PROXY_FLAGS_NONE,
                                         NULL, /* info */
                                         "com.ubuntu.SoftwareProperties",
                                         "/",
                                         "com.ubuntu.SoftwareProperties",
                                         NULL, /* cancellable */
                                         &error);

  if (proxy == NULL) {
    g_warning ("Failed to get dbus proxy for com.ubuntu.SoftwareProperties: %s", error->message);
    return FALSE;
  }

  if (value) {
    args = g_variant_new ("(bs)", TRUE, priv->token);
  } else {
    args = g_variant_new ("(bs)", FALSE, "");
  }

  gis_driver_inhibit_quit (GIS_PAGE (page)->driver);
  priv->waiting_for_livepatch_response = TRUE;
  g_dbus_proxy_call (proxy,
                     "SetLivepatchEnabled",
                     args,
                     /* Fallback to interactive authorization if the meta action didn't work */
                     G_DBUS_CALL_FLAGS_ALLOW_INTERACTIVE_AUTHORIZATION,
                     1200000, /* 20 minutes timeout should be enough to install and enable livepatch */
                     NULL, /* cancellable */
                     on_livepatch_enabled,
                     page);

  return TRUE;
}

static void
on_livepatch_token_ready (GObject      *source_object,
                          GAsyncResult *res,
                          gpointer      data)
{
  GisLivepatchPage *page = GIS_LIVEPATCH_PAGE (data);
  GisLivepatchPagePrivate *priv = gis_livepatch_page_get_instance_private (page);
  GoaPasswordBased *password_based = GOA_PASSWORD_BASED (source_object);
  g_autoptr(GError) error = NULL;

  if (!goa_password_based_call_get_password_finish (password_based, &priv->token, res, &error)) {
    g_warning ("Failed to get livepatch token: %s", error->message);
    gtk_widget_set_sensitive (priv->setup_button, TRUE);
    return;
  }

  priv->user_current_choice = TRUE;
  gtk_widget_set_visible (priv->message_box, TRUE);
  if (set_livepatch_enabled (page, TRUE)) {
    gtk_label_set_text (GTK_LABEL (priv->message_label), LIVEPATCH_ENABLING_MESSAGE);
  } else {
    gtk_label_set_text (GTK_LABEL (priv->message_label), LIVEPATCH_ENABLE_FAILURE_MESSAGE);
    gtk_widget_set_sensitive (priv->setup_button, TRUE);
  }
}

static void
goa_account_store (const gchar *account_id)
{
  GSettingsSchemaSource *source;
  g_autoptr(GSettingsSchema) schema = NULL;
  g_autoptr(GSettings) settings = NULL;

  /* Check if the gsettings schema is installed */
  source = g_settings_schema_source_get_default ();
  if (!source)
    return;
  schema = g_settings_schema_source_lookup (source, "com.ubuntu.SoftwareProperties", TRUE);
  if (!schema)
    return;

  /* If the schema is installed... */
  settings = g_settings_new ("com.ubuntu.SoftwareProperties");
  g_settings_set_string (settings, "goa-account-id", account_id);
}

static void
login_and_enable_livepatch (GisLivepatchPage *page)
{
  GisLivepatchPagePrivate *priv = gis_livepatch_page_get_instance_private (page);
  g_autofree gchar *account_id = NULL;
  GoaObject *goa_object = NULL;
  GoaPasswordBased *password_based = NULL;
  GtkWidget *dialog = NULL;
  g_autoptr(GError) error = NULL;

  /* show the login dialog if needed */
  dialog = gis_auth_dialog_new (&error);
  if (dialog) {
    gtk_window_set_transient_for (GTK_WINDOW (dialog),
                                  GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (page))));

    if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_OK) {
        account_id = gis_auth_dialog_get_account_id (GIS_AUTH_DIALOG (dialog));
        goa_object = goa_client_lookup_by_id (priv->goa_client, account_id);
    }
  } else {
    g_warning ("Failed to create the authentication dialog: %s\n", error->message);
  }

  /* login dialog was dismissed */
  if (goa_object == NULL) {
    gtk_widget_set_sensitive (priv->setup_button, TRUE);
    goto out;
  }

  priv->goa_account = goa_object_get_account (goa_object);
  goa_account_store (account_id);

  /* retrieve livepatch token */
  password_based = goa_object_peek_password_based (goa_object);

  goa_password_based_call_get_password (password_based,
                                        "livepatch",
                                        NULL /* cancellable */,
                                        on_livepatch_token_ready,
                                        page);
 out:
  g_clear_pointer (&dialog, gtk_widget_destroy);
  g_clear_object (&goa_object);
}

static void
on_livepatch_permission_acquired (GObject      *source,
                                  GAsyncResult *res,
                                  gpointer      data)
{
  GisLivepatchPage *page = GIS_LIVEPATCH_PAGE (data);
  GisLivepatchPagePrivate *priv = gis_livepatch_page_get_instance_private (page);
  g_autoptr(GError) error = NULL;
  gboolean allowed;

  allowed = g_permission_acquire_finish (priv->permission, res, &error);
  if (error) {
      if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        g_warning ("Failed to acquire permission: %s", error->message);
      else
        gtk_widget_set_sensitive (priv->setup_button, TRUE);
      return;
  }

  if (allowed)
    login_and_enable_livepatch (page);
}

static void
on_setup_button_clicked (GtkButton *button,
                         gpointer  data)
{
  GisLivepatchPage *page = GIS_LIVEPATCH_PAGE (data);
  GisLivepatchPagePrivate *priv = gis_livepatch_page_get_instance_private (page);

  gtk_widget_set_sensitive (priv->setup_button, FALSE);

  if (G_IS_PERMISSION (priv->permission) && g_permission_get_allowed (priv->permission)) {
    login_and_enable_livepatch (page);
  }
  else if (G_IS_PERMISSION (priv->permission)  && g_permission_get_can_acquire (priv->permission)) {
    g_permission_acquire_async (priv->permission,
                                NULL,
                                on_livepatch_permission_acquired,
                                page);
  } else {
     g_warning ("Could not start the attempt to acquire the permission to enable Livepatch. Fallback to per-app policy.");
     login_and_enable_livepatch (page);
  }
}

static void
on_signout_button_clicked (GtkButton        *button,
                           GisLivepatchPage *page)
{
  GisLivepatchPagePrivate *priv = gis_livepatch_page_get_instance_private (page);

  /* disable livepatch */
  priv->user_current_choice = FALSE;
  if (!priv->waiting_for_livepatch_response && is_livepatch_enabled ())
    set_livepatch_enabled (page, FALSE);

  goa_account_store ("");
  /* remove GoaAccount from system */
  goa_account_call_remove (priv->goa_account, NULL, NULL, NULL);

  /* reset the GUI */
  gtk_widget_set_sensitive (priv->setup_button, TRUE);
  gtk_widget_set_visible (priv->message_box, FALSE);

  g_clear_object (&priv->goa_account);
  g_clear_pointer (&priv->token, g_free);
}

static void
show_legal (GtkButton *button, GisLivepatchPage *page)
{
  g_autofree gchar *buffer = NULL;
  g_autofree gchar *privacy_policy = NULL;
  g_autoptr(GError) error = NULL;

  if (g_file_get_contents ("/etc/os-release", &buffer, NULL, NULL))
    privacy_policy = get_item (buffer, "PRIVACY_POLICY_URL");

  if (!gtk_show_uri_on_window (GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (page))),
                               privacy_policy,
                               GDK_CURRENT_TIME, &error)) {
    GtkWidget *dialog;
    dialog = gtk_message_dialog_new (GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (page))),
                                     GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                     GTK_MESSAGE_ERROR,
                                     GTK_BUTTONS_CLOSE,
                                     _("Failed to show Livepatch policy: %s"), error->message);
    gtk_dialog_run (GTK_DIALOG (dialog));
    gtk_widget_destroy (dialog);
    return;
  }
}

static void
gis_livepatch_page_constructed (GObject *object)
{
  GisLivepatchPage *page = GIS_LIVEPATCH_PAGE (object);
  GisLivepatchPagePrivate *priv = gis_livepatch_page_get_instance_private (page);
  g_autoptr(GError) error = NULL;

  G_OBJECT_CLASS (gis_livepatch_page_parent_class)->constructed (object);

  gis_page_set_skippable (GIS_PAGE (page), TRUE);

  priv->goa_client = goa_client_new_sync (NULL, &error);

  if (priv->goa_client == NULL) {
    g_error ("Failed to get a GoaClient: %s", error->message);
    return;
  }

  priv->permission = polkit_permission_new_sync ("com.ubuntu.welcome.livepatch", NULL, NULL, &error);
  if (priv->permission == NULL) {
    g_warning ("Could not get 'com.ubuntu.welcome.livepatch' permission: %s",
                error->message);
  }

  g_signal_connect (priv->setup_button, "clicked",
                    G_CALLBACK (on_setup_button_clicked), page);

  g_signal_connect (priv->signout_button, "clicked",
                    G_CALLBACK (on_signout_button_clicked), page);

  gis_page_set_complete (GIS_PAGE (page), TRUE);
  gtk_widget_show (GTK_WIDGET (page));
}

static void
gis_livepatch_page_dispose (GObject *object)
{
  GisLivepatchPage *page = GIS_LIVEPATCH_PAGE (object);
  GisLivepatchPagePrivate *priv = gis_livepatch_page_get_instance_private (page);

  g_clear_object (&priv->goa_client);
  g_clear_object (&priv->goa_account);
  g_clear_object (&priv->permission);
  g_clear_pointer (&priv->token, g_free);

  G_OBJECT_CLASS (gis_livepatch_page_parent_class)->dispose (object);
}

static void
gis_livepatch_page_locale_changed (GisPage *page)
{
  gis_page_set_title (GIS_PAGE (page), _("Livepatch"));
}

static void
gis_livepatch_page_class_init (GisLivepatchPageClass *klass)
{
  GisPageClass *page_class = GIS_PAGE_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  gtk_widget_class_set_template_from_resource (GTK_WIDGET_CLASS (klass), "/org/gnome/initial-setup/gis-livepatch-page.ui");

  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisLivepatchPage, setup_button);
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisLivepatchPage, message_box);
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisLivepatchPage, signout_button);
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisLivepatchPage, message_label);
  gtk_widget_class_bind_template_callback (GTK_WIDGET_CLASS (klass), show_legal);


  page_class->page_id = PAGE_ID;
  page_class->locale_changed = gis_livepatch_page_locale_changed;
  object_class->constructed = gis_livepatch_page_constructed;
  object_class->dispose = gis_livepatch_page_dispose;
}

static void
gis_livepatch_page_init (GisLivepatchPage *page)
{
  g_resources_register (livepatch_get_resource ());

  gtk_widget_init_template (GTK_WIDGET (page));
}

GisPage *
gis_prepare_livepatch_page (GisDriver *driver)
{
  if (is_livepatch_enabled () || !is_livepatch_supported ())
    return NULL;

  return g_object_new (GIS_TYPE_LIVEPATCH_PAGE,
                       "driver", driver,
                       NULL);
}
