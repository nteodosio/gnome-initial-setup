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

#include "gis-auth-dialog.h"

#include <glib/gi18n.h>
#define GOA_API_IS_SUBJECT_TO_CHANGE
#include <goa/goa.h>
#define GOA_BACKEND_API_IS_SUBJECT_TO_CHANGE
#include <goabackend/goabackend.h>

struct _GisAuthDialog
{
  GtkDialog parent_instance;

  GoaClient *goa_client;
  GtkListStore *liststore_account;

  GtkWidget *error_bar;
  GtkWidget *label_error;
  GtkWidget *label_header;
  GtkWidget *combobox_account;
  GtkWidget *label_account;
  GtkWidget *button_add_another;
  GtkWidget *button_cancel;
  GtkWidget *button_continue;

  GCancellable *cancellable;
  guint error_timeout;
};

static void gis_auth_dialog_initable_iface_init (GInitableIface *iface);

G_DEFINE_TYPE_WITH_CODE (GisAuthDialog, gis_auth_dialog, GTK_TYPE_DIALOG,
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE, gis_auth_dialog_initable_iface_init))

enum {
  COLUMN_ID,
  COLUMN_EMAIL,
  COLUMN_ACCOUNT,
  N_COLUMNS
};

static gboolean
gis_auth_dialog_ignore_account (GoaAccount *account)
{
  return g_strcmp0 (goa_account_get_provider_type (account), "ubuntusso") != 0;
}

static void gis_auth_dialog_set_error (GisAuthDialog *self,
                                       const gchar *text);

static gboolean
gis_auth_dialog_error_timeout_cb (GisAuthDialog *self)
{
  /* Hide the error bar */
  gis_auth_dialog_set_error (self, NULL);
  return G_SOURCE_REMOVE;
}

static void
gis_auth_dialog_set_error (GisAuthDialog *self,
                           const gchar *text)
{
  /* Reset current error timeout */
  if (self->error_timeout > 0) {
    g_source_remove (self->error_timeout);
    self->error_timeout = 0;
  }

  if (!text) {
    /* Hide the error bar if text is NULL */
    gtk_info_bar_set_revealed (GTK_INFO_BAR (self->error_bar), FALSE);
    gtk_widget_set_visible (self->error_bar, FALSE);
  } else {
    /* Reveal the error bar if text is not NULL */
    g_autofree gchar *markup = NULL;
    markup = g_strdup_printf (_("Failed to add an Ubuntu Single Sign-on account: %s."), text);
    gtk_label_set_markup (GTK_LABEL (self->label_error), markup);
    gtk_widget_set_visible (self->error_bar, TRUE);
    gtk_info_bar_set_revealed (GTK_INFO_BAR (self->error_bar), TRUE);
    self->error_timeout = g_timeout_add_seconds (10, (GSourceFunc) gis_auth_dialog_error_timeout_cb, self);
  }
}

static void
gis_auth_dialog_set_header (GisAuthDialog *self,
                            const gchar *text)
{
  g_autofree gchar *markup = NULL;
  markup = g_strdup_printf ("<span size='larger' weight='bold'>%s</span>", text);
  gtk_label_set_markup (GTK_LABEL (self->label_header), markup);
}

static gint
gis_auth_dialog_get_naccounts (GisAuthDialog *self)
{
  return gtk_tree_model_iter_n_children (GTK_TREE_MODEL (self->liststore_account), NULL);
}

static gboolean
gis_auth_dialog_get_nth_account_data (GisAuthDialog *self,
                                      gint n,
                                      ...)
{
  GtkTreeIter iter;
  va_list var_args;

  if (!gtk_tree_model_iter_nth_child (GTK_TREE_MODEL (self->liststore_account), &iter, NULL, n))
    return FALSE;

  va_start (var_args, n);
  gtk_tree_model_get_valist (GTK_TREE_MODEL (self->liststore_account), &iter, var_args);
  va_end (var_args);

  return TRUE;
}

static void
gis_auth_dialog_check_ui (GisAuthDialog *self,
                          gboolean select)
{
  gint naccounts = gis_auth_dialog_get_naccounts (self);

  if (naccounts == 0) {
    gis_auth_dialog_set_header (self, _("To use Livepatch, you need to use an Ubuntu One Account."));
    gtk_widget_set_visible (self->combobox_account, FALSE);
    gtk_widget_set_visible (self->label_account, FALSE);
    gtk_widget_set_visible (self->button_add_another, FALSE);
    gtk_button_set_label (GTK_BUTTON (self->button_continue), _("Sign In / Register…"));
  } else if (naccounts == 1) {
    g_autofree gchar *email = NULL;

    gis_auth_dialog_set_header (self, _("To use Livepatch, you need to use your Ubuntu One Account."));
    gtk_widget_set_visible (self->combobox_account, FALSE);
    gtk_widget_set_visible (self->label_account, TRUE);
    gtk_widget_set_visible (self->button_add_another, TRUE);
    gtk_button_set_label (GTK_BUTTON (self->button_continue), _("Continue"));
    gis_auth_dialog_get_nth_account_data (self, 0, COLUMN_EMAIL, &email, -1);
    gtk_label_set_text (GTK_LABEL (self->label_account), email);
  } else {
    gis_auth_dialog_set_header (self, _("To use Livepatch, you need to use an Ubuntu One Account."));
    gtk_widget_set_visible (self->combobox_account, TRUE);
    gtk_widget_set_visible (self->label_account, FALSE);
    gtk_widget_set_visible (self->button_add_another, TRUE);
    gtk_button_set_label (GTK_BUTTON (self->button_continue), _("Use"));

    if (select) {
      gtk_combo_box_set_active (GTK_COMBO_BOX (self->combobox_account), naccounts - 1);
    } else if (gtk_combo_box_get_active (GTK_COMBO_BOX (self->combobox_account)) == -1) {
      gtk_combo_box_set_active (GTK_COMBO_BOX (self->combobox_account), 0);
    }
  }
}

static gboolean
gis_auth_dialog_get_account_iter (GisAuthDialog *self,
                                  GoaAccount *account,
                                  GtkTreeIter *iter)
{
  gboolean valid;

  valid = gtk_tree_model_iter_nth_child (GTK_TREE_MODEL (self->liststore_account), iter, NULL, 0);

  while (valid) {
    g_autofree gchar *id;
    gtk_tree_model_get (GTK_TREE_MODEL (self->liststore_account), iter, COLUMN_ID, &id, -1);
    if (g_strcmp0 (id, goa_account_get_id (account)) == 0)
      return TRUE;
    else
      valid = gtk_tree_model_iter_next (GTK_TREE_MODEL (self->liststore_account), iter);
  }

  return FALSE;
}

static void
gis_auth_dialog_add_account (GisAuthDialog *self,
                             GoaAccount *account,
                             gboolean select)
{
  GtkTreeIter iter;

  if (gis_auth_dialog_ignore_account (account) ||
      gis_auth_dialog_get_account_iter (self, account, &iter))
    return;

  gtk_list_store_append (self->liststore_account, &iter);
  gtk_list_store_set (self->liststore_account, &iter,
                      COLUMN_ID, goa_account_get_id (account),
                      COLUMN_EMAIL, goa_account_get_presentation_identity (account),
                      COLUMN_ACCOUNT, account,
                      -1);

  gis_auth_dialog_check_ui (self, select);
}

static void
gis_auth_dialog_remove_account (GisAuthDialog *self,
                                GoaAccount *account)
{
  GtkTreeIter iter;

  if (gis_auth_dialog_ignore_account (account) ||
     !gis_auth_dialog_get_account_iter (self, account, &iter))
    return;

  gtk_list_store_remove (self->liststore_account, &iter);
  gis_auth_dialog_check_ui (self, FALSE);
}

static void
gis_auth_dialog_setup_model (GisAuthDialog *self)
{
  g_autoptr(GList) accounts = goa_client_get_accounts (self->goa_client);

  for (GList *l = accounts; l != NULL; l = l->next) {
    gis_auth_dialog_add_account (self,  goa_object_peek_account (l->data), FALSE);
    g_object_unref (l->data);
  }
}

static GoaAccount*
gis_auth_dialog_show_sso (GisAuthDialog *self,
                          GoaAccount *account)
{
  g_autoptr(GoaProvider) provider = NULL;
  g_autoptr(GError) error = NULL;
  GoaObject *goa_object = NULL;
  GoaAccount *ret = NULL;

  /* Check if ubuntusso accounts are supported */
  provider = goa_provider_get_for_provider_type ("ubuntusso");
  if (provider == NULL) {
    gis_auth_dialog_set_error (self, _("Ubuntu Single Sign-on accounts are not supported"));
    goto out;
  }

  if (!account) {
    GtkWidget *dialog = NULL;
    /* Show the login dialog */
    dialog = gtk_dialog_new_with_buttons (_("Add Account"),
                                          GTK_WINDOW (self),
                                          GTK_DIALOG_MODAL
                                          | GTK_DIALOG_DESTROY_WITH_PARENT
                                          | GTK_DIALOG_USE_HEADER_BAR,
                                          NULL, NULL);

    /* Set dialog to not resize. */
    gtk_window_set_resizable (GTK_WINDOW (dialog), FALSE);

    goa_object = goa_provider_add_account (provider,
                                           self->goa_client,
                                           GTK_DIALOG (dialog),
                                           GTK_BOX (gtk_dialog_get_content_area (GTK_DIALOG (dialog))),
                                           &error);

    gtk_widget_destroy (dialog);
  } else {
    /* Show the refresh dialog */
    goa_object = goa_client_lookup_by_id (self->goa_client, goa_account_get_id (account));
    goa_provider_refresh_account (provider,
                                  self->goa_client,
                                  goa_object,
                                  GTK_WINDOW (self),
                                  &error);
  }

  if (error) {
    if (!g_error_matches (error, GOA_ERROR, GOA_ERROR_DIALOG_DISMISSED))
      gis_auth_dialog_set_error (self, error->message);
    goto out;
  }

  ret = goa_object_get_account (goa_object);

 out:
  g_clear_object (&goa_object);
  return ret;
}

static GoaAccount*
gis_auth_dialog_get_selected_account (GisAuthDialog *self)
{
  GoaAccount *goa_account = NULL;
  gint naccounts = gis_auth_dialog_get_naccounts (self);

  if (naccounts == 1) {
    gis_auth_dialog_get_nth_account_data (self, 0, COLUMN_ACCOUNT, &goa_account, -1);
  } else  if (naccounts > 1) {
    gint active = gtk_combo_box_get_active (GTK_COMBO_BOX (self->combobox_account));
    gis_auth_dialog_get_nth_account_data (self, active, COLUMN_ACCOUNT, &goa_account, -1);
  }

  return goa_account;
}

void
gis_auth_dialog_ensure_crendentials_cb (GObject *source_object,
                                        GAsyncResult *res,
                                        gpointer user_data)
{
  GisAuthDialog *self = (GisAuthDialog*) user_data;
  GoaAccount *account = GOA_ACCOUNT (source_object);
  g_autoptr(GError) error = NULL;

  if (!goa_account_call_ensure_credentials_finish (account, NULL, res, &error)) {
    if (g_error_matches (error, GOA_ERROR, GOA_ERROR_NOT_AUTHORIZED)) {
      /* Show the refresh account is credentials are expired */
      account = gis_auth_dialog_show_sso (self, account);
      /* account is NULL in case of error or dismissal */
      if (account != NULL) {
        gtk_dialog_response (GTK_DIALOG (self), GTK_RESPONSE_OK);
        g_object_unref (account);
      }
    }
  } else {
    gtk_dialog_response (GTK_DIALOG (self), GTK_RESPONSE_OK);
  }
}

static void
gis_auth_dialog_response_if_valid (GisAuthDialog *self)
{
  GoaAccount *goa_account = gis_auth_dialog_get_selected_account (self);

  if (goa_account)
    goa_account_call_ensure_credentials (goa_account,
                                         self->cancellable,
                                         gis_auth_dialog_ensure_crendentials_cb,
                                         self);

  g_clear_object (&goa_account);
}

static void
gis_auth_dialog_account_added_cb (GoaClient *client,
                                  GoaObject *object,
                                  GisAuthDialog *self)
{
  GoaAccount *account = goa_object_peek_account (object);
  gis_auth_dialog_add_account (self, account, FALSE);
}

static void
gis_auth_dialog_account_removed_cb (GoaClient *client,
                                    GoaObject *object,
                                    GisAuthDialog *self)
{
  GoaAccount *account = goa_object_peek_account (object);
  gis_auth_dialog_remove_account (self, account);
}

static void
gis_auth_dialog_button_add_another_cb (GtkButton *button,
                                       GisAuthDialog *self)
{
  GoaAccount *account;

  g_signal_handlers_block_by_func (self->goa_client, gis_auth_dialog_account_added_cb, self);

  account = gis_auth_dialog_show_sso (self, NULL);

   /* account is NULL in case of error or dismissal */
  if (account != NULL) {
    gis_auth_dialog_add_account (self, account, TRUE);
    gis_auth_dialog_response_if_valid (self);
    g_object_unref (account);
  }

  g_signal_handlers_unblock_by_func (self->goa_client, gis_auth_dialog_account_added_cb, self);
}

static void
gis_auth_dialog_button_cancel_cb (GtkButton *button,
                                  GisAuthDialog *self)
{
  gtk_dialog_response (GTK_DIALOG (self), GTK_RESPONSE_CANCEL);
}

static void
gis_auth_dialog_button_continue_cb (GtkButton     *button,
                                    GisAuthDialog *self)
{
  gint naccounts = gis_auth_dialog_get_naccounts (self);

  if (naccounts == 0)
    gis_auth_dialog_button_add_another_cb (GTK_BUTTON (self->button_add_another), self);
  else
    gis_auth_dialog_response_if_valid (self);
}

/* GObject */

static void
gis_auth_dialog_dispose (GObject *object)
{
  GisAuthDialog *self = GIS_AUTH_DIALOG (object);

  g_clear_object (&self->goa_client);

  if (self->error_timeout > 0) {
    g_source_remove (self->error_timeout);
    self->error_timeout = 0;
  }

  g_cancellable_cancel (self->cancellable);
  g_clear_object (&self->cancellable);

  G_OBJECT_CLASS (gis_auth_dialog_parent_class)->dispose (object);
}

static void
gis_auth_dialog_class_init (GisAuthDialogClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = gis_auth_dialog_dispose;

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/initial-setup/gis-auth-dialog.ui");

  gtk_widget_class_bind_template_child (widget_class, GisAuthDialog, error_bar);
  gtk_widget_class_bind_template_child (widget_class, GisAuthDialog, label_error);
  gtk_widget_class_bind_template_child (widget_class, GisAuthDialog, label_header);
  gtk_widget_class_bind_template_child (widget_class, GisAuthDialog, combobox_account);
  gtk_widget_class_bind_template_child (widget_class, GisAuthDialog, label_account);
  gtk_widget_class_bind_template_child (widget_class, GisAuthDialog, button_add_another);
  gtk_widget_class_bind_template_child (widget_class, GisAuthDialog, button_cancel);
  gtk_widget_class_bind_template_child (widget_class, GisAuthDialog, button_continue);
  gtk_widget_class_bind_template_child (widget_class, GisAuthDialog, liststore_account);


  gtk_widget_class_bind_template_callback (GTK_WIDGET_CLASS (klass), gis_auth_dialog_button_add_another_cb);
  gtk_widget_class_bind_template_callback (GTK_WIDGET_CLASS (klass), gis_auth_dialog_button_cancel_cb);
  gtk_widget_class_bind_template_callback (GTK_WIDGET_CLASS (klass), gis_auth_dialog_button_continue_cb);
}

static void
gis_auth_dialog_init (GisAuthDialog *self)
{
  self->cancellable = g_cancellable_new ();

  gtk_widget_init_template (GTK_WIDGET (self));
  gtk_widget_grab_focus (self->button_continue);
}

/* GInitable */

static gboolean
gis_auth_dialog_initable_init (GInitable *initable,
                               GCancellable *cancellable,
                               GError  **error)
{
  GisAuthDialog *self;

  g_return_val_if_fail (GIS_IS_AUTH_DIALOG (initable), FALSE);

  self = GIS_AUTH_DIALOG (initable);

  self->goa_client = goa_client_new_sync (NULL, error);
  if (!self->goa_client)
    return FALSE;

  gis_auth_dialog_setup_model (self);
  gis_auth_dialog_check_ui (self, FALSE);

  /* Be ready to other accounts */
  g_signal_connect (self->goa_client, "account-added", G_CALLBACK (gis_auth_dialog_account_added_cb), self);
  g_signal_connect (self->goa_client, "account-removed", G_CALLBACK (gis_auth_dialog_account_removed_cb), self);

  return TRUE;
}

static void
gis_auth_dialog_initable_iface_init (GInitableIface *iface)
{
  iface->init = gis_auth_dialog_initable_init;
}

/* Public API */

GtkWidget *
gis_auth_dialog_new (GError **error)
{
  GisAuthDialog *dialog;

  dialog = g_initable_new (GIS_TYPE_AUTH_DIALOG,
                           NULL, error,
                           "title", "",
                           NULL);

  return GTK_WIDGET (dialog);
}

gchar *
gis_auth_dialog_get_account_id (GisAuthDialog *self)
{
  GoaAccount *goa_account;
  gchar *account_id = NULL;

  g_return_val_if_fail (GIS_IS_AUTH_DIALOG (self), NULL);

  goa_account = gis_auth_dialog_get_selected_account (self);
  if (goa_account)
    account_id = goa_account_dup_id (goa_account);

  g_clear_object (&goa_account);
  return account_id;
}
