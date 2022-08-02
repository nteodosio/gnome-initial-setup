/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright (C) 2022 Canonical Ltd.
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

/* Canonical Ubuntu Pro page {{{1 */

#define PAGE_ID "UbuntuPro"

#include "config.h"
#include "gis-auth-dialog.h"
#include "gis-ubuntupro-page.h"
#include "ubuntupro-resources.h"

#include <glib/gi18n.h>
#include <gio/gio.h>
#include <polkit/polkit.h>
#include <curl/curl.h>
#include <json-glib/json-glib.h>
#include <libsoup/soup.h>

struct _GisUbuntuProPagePrivate {
  GtkWidget *enable_pro_select;
  GtkWidget *skip_pro_select;
  GtkWidget *offline_warning;
  GtkWidget *pro_email_entry;
  GtkWidget *pro_register_label;
  GtkWidget *pro_status_image;
  GtkWidget *token_attach_label;
  GtkWidget *pin_label;
  GtkWidget *skip_choice;
  GtkWidget *generate_newcode_button;
  GtkWidget *simulate_action;
  GtkWidget *token_field;
  GtkWidget *token_button;

  guint ua_desktop_watch;
  gint64 timeout;
  gchar *token;

  GPermission *permission;
};
typedef struct _GisUbuntuProPagePrivate GisUbuntuProPagePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (GisUbuntuProPage, gis_ubuntupro_page, GIS_TYPE_PAGE);

static gboolean magic_parser(void*, size_t, RestJSONResponse*);

static gboolean
get_ubuntu_advantage_attached(gboolean *attached)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(GDBusConnection) bus = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
    if (bus == NULL) {
        g_warning ("Failed to get system bus: %s", error->message);
        return FALSE;
    }

    g_autoptr(GVariant) result = g_dbus_connection_call_sync (bus,
        "com.canonical.UbuntuAdvantage",
        "/com/canonical/UbuntuAdvantage/Manager",
        "org.freedesktop.DBus.Properties",
        "Get",
        g_variant_new ("(ss)", "com.canonical.UbuntuAdvantage.Manager", "Attached"),
        G_VARIANT_TYPE ("(v)"),
        G_DBUS_CALL_FLAGS_NONE,
        -1,
        NULL,
        &error);
    if (result == NULL) {
        g_warning ("Failed to contact Ubuntu Advantage D-Bus service: %s", error->message);
        return FALSE;
    }

    g_autoptr(GVariant) value = NULL;
    g_variant_get (result, "(v)", &value);
    if (!g_variant_is_of_type (value, G_VARIANT_TYPE("b"))) {
        g_warning ("Attached property has wrong type");
        return FALSE;
    }
    g_variant_get (value, "b", attached);

    return TRUE;
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
is_ubuntupro_supported ()
{
  return is_lts ();
}

static void
network_status_changed (GNetworkMonitor *monitor,
                        gboolean         available,
                        gpointer         user_data)
{
  GisUbuntuProPagePrivate *priv = user_data;

  if (!available) {
    gtk_widget_set_sensitive (priv->enable_pro_select, FALSE);
    gtk_widget_set_sensitive (priv->pro_email_entry, FALSE);
    gtk_widget_set_sensitive (priv->pro_register_label, FALSE);
    gtk_widget_show (GTK_WIDGET (priv->offline_warning));
    gtk_widget_show (GTK_WIDGET (priv->pro_status_image));
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (priv->skip_pro_select), TRUE);
    gtk_image_set_from_icon_name (GTK_IMAGE(priv->pro_status_image), "gtk-dialog-warning", GTK_ICON_SIZE_DND);
  }
  else {
    gtk_widget_set_sensitive (priv->enable_pro_select, TRUE);
    gtk_widget_set_sensitive (priv->pro_email_entry, TRUE);
    gtk_widget_set_sensitive (priv->pro_register_label, TRUE);
    gtk_widget_hide (GTK_WIDGET (priv->offline_warning));
    gtk_widget_hide (GTK_WIDGET (priv->pro_status_image));
  }
}

static void
on_uad_appeared (GDBusConnection *connection,
                    const gchar *name,
                    const gchar *name_owner,
                    gpointer user_data)
{
  GisUbuntuProPage *page = user_data;

  g_print("on_uad_appeared\n");
}

static void
on_uad_disappeared (GDBusConnection *unused1,
                       const gchar *unused2,
                       gpointer user_data)
{
  GisUbuntuProPage *page = user_data;
  GisUbuntuProPagePrivate *priv = gis_ubuntupro_page_get_instance_private (page);

  g_print("on_uad_disappeared\n");
  gtk_widget_set_visible (GTK_WIDGET (page), FALSE);
}

static void
gis_ubuntupro_page_constructed (GObject *object)
{
  GisUbuntuProPage *page = GIS_UBUNTUPRO_PAGE (object);
  GisUbuntuProPagePrivate *priv = gis_ubuntupro_page_get_instance_private (page);
  g_autoptr(GError) error = NULL;
  GDBusConnection *connection;
  GNetworkMonitor *network_monitor = g_network_monitor_get_default ();

  G_OBJECT_CLASS (gis_ubuntupro_page_parent_class)->constructed (object);

  gis_page_set_skippable (GIS_PAGE (page), TRUE);
  
  priv->permission = polkit_permission_new_sync ("com.ubuntu.welcome.ubuntupro", NULL, NULL, &error); /* TOFIX */
  if (priv->permission == NULL) {
    g_warning ("Could not get 'com.ubuntu.welcome.ubuntupro' permission: %s",
                error->message);
    g_clear_error (&error);
  }

  g_signal_connect (network_monitor, "network-changed",
                    G_CALLBACK (network_status_changed), priv);

  gis_page_set_complete (GIS_PAGE (page), TRUE);

  if (!g_network_monitor_get_network_available (network_monitor)) {
    gtk_widget_set_sensitive (priv->enable_pro_select, FALSE);
    gtk_widget_set_sensitive (priv->pro_email_entry, FALSE);
    gtk_widget_set_sensitive (priv->pro_register_label, FALSE);
    gtk_widget_show (GTK_WIDGET (priv->offline_warning));
    gtk_widget_show (GTK_WIDGET (priv->pro_status_image));
    //gtk_image_set_from_icon_name (GTK_IMAGE(priv->pro_status_image), "gtk-yes", GTK_ICON_SIZE_DND);
  }

  gtk_widget_show (GTK_WIDGET (page));
}

static void
gis_ubuntupro_page_dispose (GObject *object)
{
  GisUbuntuProPage *page = GIS_UBUNTUPRO_PAGE (object);
  GisUbuntuProPagePrivate *priv = gis_ubuntupro_page_get_instance_private (page);

  g_clear_object (&priv->permission);

  G_OBJECT_CLASS (gis_ubuntupro_page_parent_class)->dispose (object);
}

static void
gis_ubuntupro_page_locale_changed (GisPage *page)
{
  gis_page_set_title (GIS_PAGE (page), _("Ubuntu Pro"));
}

static gssize
make_rest_req(void *buf, size_t bufsize, const char* type, const char* where,
              const char* header_name, const char* header)
{
  SoupSession *session = soup_session_new();
  SoupMessage *msg;
  const char *field = "foo";
  GError *error = NULL;
  msg = soup_message_new(type, where);
  soup_message_set_request(msg, "text/plain", SOUP_MEMORY_COPY, field,
    strlen(field));
  if (header_name != NULL && header != NULL){
    soup_message_headers_append (msg->request_headers, header_name, header);
  }
  GInputStream *stream = soup_session_send(session, msg, NULL, &error);
  gssize nbytes = g_input_stream_read(stream, buf, bufsize, NULL, &error);
  if (nbytes >= bufsize || error != NULL){
    g_warning("Didn't get a valid response for token request.");
    return -1;
  }
  return nbytes;
}

static gboolean
poll_token_attach (GisUbuntuProPagePrivate *priv)
{
  gboolean ret = FALSE;
  size_t bufsize = 1024;
  void *buf = malloc(bufsize);
  const char *header_name = "Authorization";
  gchar header[128] = "Bearer ";
  strcat(header, priv->token);
//Send: curl -X GET -H 'Authorization: Bearer >>TOKEN<<' 
//unauthorized response: {"code":"unauthorized","message":"unauthorized","traceId":"280a7ed5-fdb0-4ac4-be23-d26deafb4616"}
//authorized response, has the same structure of the POST if pending: {"expires":"2022-07-15T17:45:51.902Z","expiresIn":567,"token":"M13TDXUcoPrrV9guPBmps6fUJpxxpc","userCode":"8FHI4I"}
//authorized and activated response: contains two additional fields: "contractID": "CONTRACT_ID", "contractToken": "CONTRACT_TOKEN"

  gssize nbytes = make_rest_req(buf, bufsize, "GET",
    "https://contracts.staging.canonical.com/v1/magic-attach",
    header_name, header);
  if (nbytes > 0){
    RestJSONResponse resp;
    if (!magic_parser(buf, nbytes, &resp)){
      g_warning("Couldn't parse response.\n");
    } else if (resp.contractID != NULL){
      g_print("Attached with contract ID %s\n", resp.contractID);
      ret = TRUE;
    }
  }

  free(buf);
  return ret;
}

static gboolean
token_countdown (gpointer data)
{
  GisUbuntuProPagePrivate *priv = gis_ubuntupro_page_get_instance_private (data);
  GtkLabel *str_label;
  
  priv->timeout = priv->timeout - 1;

  if (priv->timeout <= 0) {
    g_print("hit the timeout\n");
    str_label = g_strdup_printf ("To enable Ubuntu Pro, login at <small><a href='https://ubuntu.com/pro/attach/'>ubuntu.com/pro/attach</a></small> and verify the Attached Code below. <b>Code expired</b>");
    gtk_label_set_markup (GTK_LABEL (priv->token_attach_label), str_label);
    gtk_widget_show (GTK_WIDGET (priv->generate_newcode_button));
    return FALSE;
  } else if (priv->timeout % 10 == 0) {
  /* Don't poll the server every second, only every 10 seconds. */
    gboolean attached = poll_token_attach(priv);
    if (attached) {
      g_print("attached!\n");
      return FALSE;
    }
  }

  str_label = g_strdup_printf ("To enable Ubuntu Pro, login at <small><a href='https://ubuntu.com/pro/attach/'>ubuntu.com/pro/attach</a></small> and verify the Attached Code below. <b>Expire in %ds</b>", priv->timeout);
  gtk_label_set_markup (GTK_LABEL (priv->token_attach_label), str_label);

  return TRUE;
}

static gboolean
magic_parser(void* ptr,              //pointer to actual response
             size_t nmemb,           //size of data to which ptr points
             RestJSONResponse *resp) //relevant response fields
{
    const char *data = (const char*)ptr; // Not null terminated, use nmemb

    JsonParser *parser;
    JsonNode *root;
    GError *error;

    parser = json_parser_new();
    error = NULL;
    json_parser_load_from_data(parser, data, nmemb, &error);
    if (error){
        g_warning("Couldn't parse magic token JSON; %s\n", error->message);
        g_error_free(error);
        g_object_unref(parser);
        return FALSE;
    }
    root = json_parser_get_root(parser);
    if (!JSON_NODE_HOLDS_OBJECT(root)){
        g_warning("Invalid magic token JSON\n");
        return FALSE;
    }

    JsonObject *response = json_node_get_object(root);
    resp->expiresIn = json_object_has_member(response, "expiresIn") ?
        json_object_get_int_member(response, "expiresIn") :
        0;
    resp->token = json_object_has_member(response, "token") ?
        strdup(json_object_get_string_member(response, "token")) :
        NULL;
    resp->code = json_object_has_member(response, "userCode") ?
        strdup(json_object_get_string_member(response, "userCode")) :
        NULL;
    resp->contractID = json_object_has_member(response, "contractID") ?
        strdup(json_object_get_string_member(response, "contractID")) :
        NULL;
    resp->contractToken = json_object_has_member(response, "contractToken") ?
        strdup(json_object_get_string_member(response, "contractToken")) :
        NULL;

    g_object_unref(parser);
    return TRUE;
}

static void
next_page (GtkButton *button, GisUbuntuProPage *page)
{
  GisUbuntuProPagePrivate *priv = gis_ubuntupro_page_get_instance_private (page);

  gtk_widget_set_visible (GTK_WIDGET (priv->token_button), TRUE);
  gtk_widget_set_visible (GTK_WIDGET (priv->token_field), TRUE);
  gtk_widget_set_visible (GTK_WIDGET (priv->simulate_action), TRUE);
  gtk_widget_set_visible (GTK_WIDGET (button), FALSE);
}

static void
request_magic_attach (GtkButton *button, GisUbuntuProPage *page)
{
  GisUbuntuProPagePrivate *priv = gis_ubuntupro_page_get_instance_private (page);
  char *command = NULL;

  g_print ("Request magic attach\n");
  gtk_widget_show (GTK_WIDGET (priv->pin_label));
  gtk_widget_show (GTK_WIDGET (priv->token_attach_label));
  gtk_widget_hide (GTK_WIDGET (priv->pro_register_label));
  gtk_widget_hide (GTK_WIDGET (priv->skip_choice));

  size_t bufsize = 1024;
  void *buf = malloc(bufsize);
  gssize nbytes = make_rest_req(buf, bufsize, "POST",
    "https://contracts.staging.canonical.com/v1/magic-attach", NULL, NULL);
  if (nbytes > 0){
    RestJSONResponse resp;
    if (!magic_parser(buf, nbytes, &resp)){
      g_warning("Couldn't parse response.\n");
      return;
    }
    gtk_label_set_text (GTK_LABEL (priv->pin_label), resp.code);
    priv->timeout = resp.expiresIn;
    priv->token = resp.token; //don't free, used by poll callback
    g_timeout_add_seconds (1, token_countdown, page);
    free(resp.code);
  }

  free(buf);
}

static gboolean
ua_attach(gchar *token){
    GVariant        *result;
    GDBusConnection *bus;
    GError          *error = NULL;

    bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &error);
    if (bus == NULL){
        g_warning("Failed to get system bus: %s", error->message);
        return(FALSE);
    }
    result = g_dbus_connection_call_sync(bus,
        "com.canonical.UbuntuAdvantage",
        "/com/canonical/UbuntuAdvantage/Manager",
        "com.canonical.UbuntuAdvantage.Manager",
        "Attach",
        g_variant_new("(s)", token),
        G_VARIANT_TYPE("()"),
        G_DBUS_CALL_FLAGS_NONE,
        -1,
        NULL,
        &error);
    if (result == NULL){
        g_warning("Failed to contact Ubuntu Advantage D-Bus service: %s",
                  error->message);
        return(FALSE);
    }
    return(TRUE);
}

static void
request_token_attach (GtkButton *button, GisUbuntuProPage *page)
{
  GisUbuntuProPagePrivate *priv = gis_ubuntupro_page_get_instance_private (page);
  gtk_widget_set_sensitive (priv->token_button, FALSE);
  gchar *token = gtk_entry_get_text(GTK_ENTRY(priv->token_field));
  if (ua_attach(token)){
    g_print("attached successfully\n");
    //Go to next page
  } else {
    g_print("failed to attach\n");
    gtk_widget_set_sensitive (priv->token_button, TRUE);
  }
}

static void
on_enablepro_toggled (GtkWidget *button, GisUbuntuProPage *page)
{
  GisUbuntuProPagePrivate *priv = gis_ubuntupro_page_get_instance_private (page);

  if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button))) {
    gtk_widget_set_sensitive (priv->pro_email_entry, TRUE);
    gtk_widget_set_sensitive (priv->simulate_action, TRUE);
  }
  else {
    gtk_widget_set_sensitive (priv->pro_email_entry, FALSE);
    gtk_widget_set_sensitive (priv->simulate_action, FALSE);
  }
}

/* non working yet hack trying to override the next action */
static gboolean
gis_ubuntupro_page_apply (GisPage      *page,
                         GCancellable *cancellable)
{
    g_print("apply\n");
//  GisPage *account_page = GIS_PAGE (data);
  GisUbuntuProPagePrivate *priv = gis_ubuntupro_page_get_instance_private (page);
  return TRUE;
/*  return gis_ubuntupro_page_apply (GIS_UBUNTUPRO_PAGE (priv->page), cancellable,
                                            ubuntupro_apply_complete, page);
                                            */
}

static void
gis_ubuntupro_page_class_init (GisUbuntuProPageClass *klass)
{
  GisPageClass *page_class = GIS_PAGE_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  gtk_widget_class_set_template_from_resource (GTK_WIDGET_CLASS (klass), "/org/gnome/initial-setup/gis-ubuntupro-page.ui");

  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisUbuntuProPage, enable_pro_select);
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisUbuntuProPage, skip_pro_select);
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisUbuntuProPage, offline_warning);
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisUbuntuProPage, pro_email_entry);
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisUbuntuProPage, pro_register_label);
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisUbuntuProPage, pro_status_image);
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisUbuntuProPage, token_attach_label);
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisUbuntuProPage, pin_label);
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisUbuntuProPage, skip_choice);
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisUbuntuProPage, generate_newcode_button);
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisUbuntuProPage, token_field);
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisUbuntuProPage, token_button);
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisUbuntuProPage, simulate_action);
  gtk_widget_class_bind_template_callback (GTK_WIDGET_CLASS (klass), request_token_attach);
  gtk_widget_class_bind_template_callback (GTK_WIDGET_CLASS (klass), request_magic_attach);
  gtk_widget_class_bind_template_callback (GTK_WIDGET_CLASS (klass), next_page);
  gtk_widget_class_bind_template_callback (GTK_WIDGET_CLASS (klass), on_enablepro_toggled);
  
  page_class->page_id = PAGE_ID;
  page_class->locale_changed = gis_ubuntupro_page_locale_changed;
  page_class->apply = gis_ubuntupro_page_apply;  
  object_class->constructed = gis_ubuntupro_page_constructed;
  object_class->dispose = gis_ubuntupro_page_dispose;
}

static void
gis_ubuntupro_page_init (GisUbuntuProPage *page)
{
  g_resources_register (ubuntupro_get_resource ());

  gtk_widget_init_template (GTK_WIDGET (page));
}

GisPage *
gis_prepare_ubuntu_pro_page (GisDriver *driver)
{
    gboolean attached;
    if (!get_ubuntu_advantage_attached (&attached)) {
        g_print("the ua status fetch failed\n");
        return NULL;
    }
    if (attached) {
        g_print("ua is attached\n");
        //return NULL;
    }

  if (!is_ubuntupro_supported ())
    return NULL;

  return g_object_new (GIS_TYPE_UBUNTUPRO_PAGE,
                       "driver", driver,
                       NULL);
}
