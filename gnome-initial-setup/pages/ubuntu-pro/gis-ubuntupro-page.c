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
#include <json-glib/json-glib.h>
#include <libsoup/soup.h>

static gboolean magic_parser(void*, size_t, RestJSONResponse*);

struct _GisUbuntuProPagePrivate {
  GtkWidget *page1;
  GtkWidget *page2;
  GtkWidget *page3;
  GtkWidget *stack;

  guint current_page;
};
typedef struct _GisUbuntuProPagePrivate GisUbuntuProPagePrivate;

struct _GisUbuntuProPage1Private {
  GtkWidget *enable_pro_select;
  GtkWidget *skip_pro_select;
  GtkWidget *offline_warning;
  GtkWidget *pro_status_image;

  GPermission *permission;
  GCancellable *cancellable;
  GisPageApplyCallback apply_complete_callback;
  gpointer apply_complete_data;
};
typedef struct _GisUbuntuProPage1Private GisUbuntuProPage1Private;

struct _GisUbuntuProPage2Private {
  GtkWidget *pin_label;
  GtkWidget *token_field;
  GtkWidget *token_status;
  GtkWidget *token_status_icon;
  GtkWidget *pin_hint;
  GtkWidget *pin_status;
  GtkWidget *pin_status_icon;

  gint64 timeout;
  gchar *token;
};
typedef struct _GisUbuntuProPage2Private GisUbuntuProPage2Private;

struct _GisUbuntuProPage3Private {
  GtkWidget *enabled_services;
  GtkWidget *enabled_services_header;
  GtkWidget *available_services;
  GtkWidget *available_services_header;
  GtkWidget *contract_name;
  GtkWidget *checkmark;
};
typedef struct _GisUbuntuProPage3Private GisUbuntuProPage3Private;

G_DEFINE_TYPE_WITH_PRIVATE (GisUbuntuProPage, gis_ubuntupro_page, GIS_TYPE_PAGE);
G_DEFINE_TYPE_WITH_PRIVATE (GisUbuntuProPage1, gis_ubuntupro_page1, GTK_TYPE_BIN);
G_DEFINE_TYPE_WITH_PRIVATE (GisUbuntuProPage2, gis_ubuntupro_page2, GTK_TYPE_BIN);
G_DEFINE_TYPE_WITH_PRIVATE (GisUbuntuProPage3, gis_ubuntupro_page3, GTK_TYPE_BIN);

/* From Vifm src/utils/str.c */
int
strappend(char **str, size_t *len, const char suffix[]){
	const size_t suffix_len = strlen(suffix);
	char *const new = realloc(*str, *len + suffix_len + 1);
	if (new == NULL){
		return(1);
	}

	strcpy(new + *len, suffix);
	*str = new;
	*len += suffix_len;

	return(0);
}

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
  GisUbuntuProPage1Private *priv = user_data;

  if (!available) {
    gtk_widget_set_sensitive (priv->enable_pro_select, FALSE);
    gtk_widget_show (GTK_WIDGET (priv->offline_warning));
    gtk_widget_show (GTK_WIDGET (priv->pro_status_image));
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (priv->skip_pro_select), TRUE);
    gtk_image_set_from_icon_name (GTK_IMAGE(priv->pro_status_image), "gtk-dialog-warning", GTK_ICON_SIZE_DND);
  }
  else {
    gtk_widget_set_sensitive (priv->enable_pro_select, TRUE);
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
  GisUbuntuProPage *main_page = GIS_UBUNTUPRO_PAGE (object);
  GisUbuntuProPagePrivate *main_page_priv = gis_ubuntupro_page_get_instance_private (main_page);
  GisUbuntuProPage1 *page = GIS_UBUNTUPRO_PAGE1 (main_page_priv->page1);
  GisUbuntuProPage1Private *priv = gis_ubuntupro_page1_get_instance_private (page);

  gis_page_set_complete (GIS_PAGE (main_page), TRUE);
  gtk_widget_show (GTK_WIDGET (main_page));

  g_autoptr(GError) error = NULL;
  GDBusConnection *connection;
  GNetworkMonitor *network_monitor = g_network_monitor_get_default ();

  G_OBJECT_CLASS (gis_ubuntupro_page_parent_class)->constructed (object);

  gis_page_set_skippable (GIS_PAGE (main_page), TRUE);
  
  priv->permission = polkit_permission_new_sync ("com.ubuntu.welcome.ubuntupro", NULL, NULL, &error); /* TOFIX */
  if (priv->permission == NULL) {
    g_warning ("Could not get 'com.ubuntu.welcome.ubuntupro' permission: %s",
                error->message);
    g_clear_error (&error);
  }

  g_signal_connect (network_monitor, "network-changed",
                    G_CALLBACK (network_status_changed), priv);

  gis_page_set_complete (GIS_PAGE (main_page), TRUE);

  if (!g_network_monitor_get_network_available (network_monitor)) {
    gtk_widget_set_sensitive (priv->enable_pro_select, FALSE);
    gtk_widget_show (GTK_WIDGET (priv->offline_warning));
    gtk_widget_show (GTK_WIDGET (priv->pro_status_image));
    //gtk_image_set_from_icon_name (GTK_IMAGE(priv->pro_status_image), "gtk-yes", GTK_ICON_SIZE_DND);
  }

  main_page_priv->current_page = 1;
  gtk_widget_show (GTK_WIDGET (main_page));
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
  soup_message_set_request_body(msg, "text/plain", NULL, -1);
  if (header_name != NULL && header != NULL){
    soup_message_headers_append (soup_message_get_request_headers(msg), header_name, header);
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
poll_token_attach (GisUbuntuProPage2Private *priv)
{
  gboolean ret = FALSE;
  size_t bufsize = 1024;
  void *buf = malloc(bufsize);
  const char *header_name = "Authorization";
  char *header = strdup("Bearer ");
  size_t len = strlen(header);
  strappend(&header, &len, priv->token);

/* curl -X GET -H 'Authorization: Bearer >>TOKEN<<'
 *
 * Sample unauthorized response:
 * {
 *   "code":"unauthorized",
 *   "message":"unauthorized",
 *   "traceId":"280a7ed5-fdb0-4ac4-be23-d26deafb4616"
 * }
 *
 * Sample authorized response:
 * {
 *   "expires":"2022-07-15T17:45:51.902Z",
 *   "expiresIn":567,
 *   "token":"M1guPBps6fUJpxasdasdxpc",
 *   "userCode":"8FHI4I"
 * }
 *
 * Sample authorized and activated response:
 * {
 *   "expires":"2022-07-15T17:45:51.902Z",
 *   "expiresIn":567,
 *   "token":"M1guPBps6fUJpxasdasdxpc",
 *   "userCode":"8FHI4I",
 *   "contractID":"CONTRACT_ID",
 *   "contractToken":"CONTRACT_TOKEN"
 * }
 *
 */
  gssize nbytes = make_rest_req(buf, bufsize, "GET",
    "https://contracts.staging.canonical.com/v1/magic-attach",
    header_name, header);
  free(header);
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
  GisUbuntuProPage2Private *priv = gis_ubuntupro_page2_get_instance_private (data);
  gchar *str_label;
  
  priv->timeout = priv->timeout - 1;

  if (priv->timeout <= 0) {
    str_label = g_strdup_printf ("<span foreground=\"#900000\"><b>Code expired</b></span>");
    gtk_label_set_markup (GTK_LABEL (priv->pin_status), str_label);
    gtk_image_set_from_resource(GTK_IMAGE(priv->pin_status_icon), "/org/gnome/initial-setup/fail.svg");
    gtk_widget_set_visible (GTK_WIDGET (priv->pin_status_icon), TRUE);
    gtk_label_set_text(GTK_LABEL(priv->pin_hint), "Click the button to generate a new code.");
    return FALSE;
  } else if (priv->timeout % 10 == 0) {
  /* Don't poll the server every second, only every 10 seconds. */
    gboolean attached = poll_token_attach(priv);
    if (attached) {
      str_label = g_strdup_printf ("<span foreground=\"#008000\"><b>Code verified</b></span>");
      gtk_label_set_markup (GTK_LABEL (priv->pin_status), str_label);
      gtk_image_set_from_resource(GTK_IMAGE(priv->pin_status_icon), "/org/gnome/initial-setup/checkmark.svg");
      gtk_widget_set_visible (GTK_WIDGET (priv->pin_status_icon), TRUE);
      g_free(str_label);
      return FALSE;
    }
  }

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

char*
parse_ua_status(){
    size_t len;
    char   buffer[1024], *output = {0};

    FILE *stream = popen("ua status --all --format=json", "r");
    if (stream == NULL){
        exit(1);
    }
    while (fgets(buffer, sizeof(buffer), stream) != NULL){
        len = (output != NULL ? strlen(output) : 0);
        strappend(&output, &len, buffer);
    }
    if (pclose(stream) != 0){
        g_warning("Couldn't execute ua, is ubuntu-advantage-tools installed?");
    }

    return(output);
}

static void
display_checkmark(GisUbuntuProPage3Private *priv){
  GError *error = NULL;
  GdkPixbuf *check_pixbuf = gdk_pixbuf_new_from_resource_at_scale (
      "/org/gnome/initial-setup/checkmark.svg",
      30,
      -1,
      TRUE,
      &error
  );

  if (check_pixbuf != NULL){
    gtk_image_set_from_pixbuf(GTK_IMAGE(priv->checkmark), check_pixbuf);
  } else {
    g_warning("Unable to load pixbuf: %s\n", error->message);
  }
  g_error_free(error);
  g_free(check_pixbuf);
}

static gboolean
display_ua_services(GisUbuntuProPage3Private *priv){
    JsonParser  *parser;
    JsonNode    *root_node;
    JsonObject  *root, *services, *contract;
    JsonArray   *services_array;
    GError      *error;
    guint       i, n_services;
    size_t      len;
    const char  *status, *description, *available, *contract_name;
    char        *enabled_str = {0}, *available_str = {0};
    gboolean    ret;

    char *str = parse_ua_status();

    parser = json_parser_new();
    error = NULL;
    json_parser_load_from_data(parser, str, strlen(str), &error);
    if (error){
        g_warning("Couldn't parse magic token JSON; %s\n", error->message);
        ret = FALSE;
        goto end;
    }
    root_node = json_parser_get_root(parser);
    if (!JSON_NODE_HOLDS_OBJECT(root_node)){
        g_warning("Invalid magic token JSON\n");
        ret = FALSE;
        goto end;
    }

    root = json_node_get_object(root_node);

    contract = json_object_get_object_member(root, "contract");
    contract_name = json_object_get_string_member(contract, "name");
    gtk_label_set_text(GTK_LABEL(priv->contract_name), contract_name);

    services_array = json_object_get_array_member(root, "services");
    n_services = json_array_get_length(services_array);

    /* Get services description, status and availability */
    for (i = 0; i < n_services; i++){
        services = json_array_get_object_element(services_array, i);
        if (
            json_object_has_member(services, "description") &&
            json_object_has_member(services, "status") &&
            json_object_has_member(services, "available")
        ) {
            description = json_object_get_string_member(services, "description");
            status = json_object_get_string_member(services, "status");
            available = json_object_get_string_member(services, "available");
            if (strcmp(status, "enabled") == 0){
              len = (enabled_str != NULL ? strlen(enabled_str) : 0);
              strappend(&enabled_str, &len, description);
              strappend(&enabled_str, &len, "\n");
            } else if (strcmp(available, "yes") == 0) {
              len = (available_str != NULL ? strlen(available_str) : 0);
              strappend(&available_str, &len, description);
              strappend(&available_str, &len, "\n");
            }
        }
    }
    ret = i;

    /* Display enabled and disabled but avaialable services */
    if (enabled_str == NULL) {
      gtk_widget_destroy(GTK_WIDGET(priv->enabled_services_header));
    } else {
      gtk_label_set_text(GTK_LABEL(priv->enabled_services), enabled_str);
      free(enabled_str);
    }
    if (available_str == NULL) {
      gtk_widget_destroy(GTK_WIDGET(priv->available_services_header));
    } else {
      gtk_label_set_text(GTK_LABEL(priv->available_services), available_str);
      free(available_str);
    }

end:
    free(str);
    g_object_unref(parser);
    g_error_free(error);
    return ret;
}

static void
request_magic_attach (GisUbuntuProPage2 *page)
{
  GisUbuntuProPage2Private *priv = gis_ubuntupro_page2_get_instance_private (page);

  g_print ("Request magic attach\n");
  gtk_widget_show (GTK_WIDGET (priv->pin_label));

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

static void
on_ua_attach_requested (GObject *source,
                        GAsyncResult *result,
                        gpointer user_data)
{
  GisUbuntuProPage2Private *priv = user_data;
  GError *error = NULL;
  GVariant *retval;
  gchar *str_label;

  retval = g_dbus_connection_call_finish (G_DBUS_CONNECTION (source), result, &error);
  if (retval == NULL) {
    g_warning ("Failed to attach token: %s", error->message);
    g_error_free (error);

    str_label = g_strdup_printf("<span foreground=\"#900000\"><b>Failed to attach</b></span>");
    gtk_label_set_markup (GTK_LABEL (priv->token_status), str_label);
    gtk_image_set_from_resource(GTK_IMAGE(priv->token_status_icon), "/org/gnome/initial-setup/fail.svg");
    gtk_widget_set_sensitive (priv->token_field, TRUE);
  } else {
    g_variant_unref (retval);

    str_label = g_strdup_printf("<span foreground=\"#008000\"><b>Valid token</b></span>");
    gtk_image_set_from_resource(GTK_IMAGE(priv->token_status_icon), "/org/gnome/initial-setup/checkmark.svg");
    gtk_label_set_markup (GTK_LABEL (priv->token_status), str_label);
  }
  g_free(str_label);
}

static void
ua_attach(const gchar *token, GisUbuntuProPage2Private *priv){
  GDBusConnection *bus;
  GError          *error = NULL;

  bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &error);
  if (bus == NULL){
      g_warning("Failed to get system bus: %s", error->message);
  } else {
    g_dbus_connection_call(bus,
      "com.canonical.UbuntuAdvantage",
      "/com/canonical/UbuntuAdvantage/Manager",
      "com.canonical.UbuntuAdvantage.Manager",
      "Attach",
      g_variant_new("(s)", token),
      G_VARIANT_TYPE("()"),
      G_DBUS_CALL_FLAGS_NONE,
      -1,
      NULL,
      on_ua_attach_requested,
      priv);
  }
}

void
request_token_attach (GtkButton *button, GisUbuntuProPage2 *page)
{
  gchar *str_label;
  GisUbuntuProPage2Private *priv = gis_ubuntupro_page2_get_instance_private (page);

  gtk_widget_set_sensitive (priv->token_field, FALSE);
  const gchar *token = gtk_entry_get_text(GTK_ENTRY(priv->token_field));
  ua_attach(token, priv);
  str_label = g_strdup_printf("<span foreground=\"#999999\"><b>Verifying token</b></span>");
  gtk_label_set_markup (GTK_LABEL (priv->token_status), str_label);
  gtk_widget_set_visible (GTK_WIDGET (priv->token_status_icon), TRUE);
  gtk_image_set_from_resource(GTK_IMAGE(priv->token_status_icon), "/org/gnome/initial-setup/loading.svg");
  g_free(str_label);
}

void
on_magic_toggled (GtkButton *button, GisUbuntuProPage2 *page)
{
  GisUbuntuProPage2Private *priv = gis_ubuntupro_page2_get_instance_private (page);

  gtk_widget_is_visible(priv->pin_label);
  const gchar *label = gtk_label_get_text (GTK_LABEL (priv->pin_label));
  if (*label == '\0' || priv->timeout <= 0){
    request_magic_attach(page);
    gtk_widget_set_visible (GTK_WIDGET (priv->pin_status_icon), FALSE);
    gtk_label_set_text (GTK_LABEL (priv->pin_hint), "Enter code on ubuntu.com/pro/attach");
    gtk_label_set_text(GTK_LABEL(priv->pin_status), "");
  }
}

void
on_token_toggled (GtkButton *button, GisUbuntuProPage2 *page)
{
  GisUbuntuProPage2Private *priv = gis_ubuntupro_page2_get_instance_private (page);

  if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button))) {
    gtk_widget_set_sensitive(priv->token_field, TRUE);
  } else {
    gtk_widget_set_sensitive(priv->token_field, FALSE);
  }
}

static gboolean
gis_ubuntupro_page_apply (GisPage      *gis_page,
                          GCancellable *cancellable)
{
  GisUbuntuProPage *page = GIS_UBUNTUPRO_PAGE (gis_page);
  GisUbuntuProPagePrivate *priv = gis_ubuntupro_page_get_instance_private (page);
  if (priv->current_page == 1) {
    GisUbuntuProPage1 *page1 = GIS_UBUNTUPRO_PAGE1 (priv->page1);
    GisUbuntuProPage1Private *priv1 = gis_ubuntupro_page1_get_instance_private (page1);
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON (priv1->skip_pro_select))) {
      /* Bails out of UbuntuPro if skip button was selected */
      return FALSE;
    }

    /* Request magic token already and advance to next local page */
    on_magic_toggled(NULL, GIS_UBUNTUPRO_PAGE2(priv->page2));
    gis_page_apply_complete (GIS_PAGE (page), FALSE);
    gtk_stack_set_visible_child (GTK_STACK (priv->stack), priv->page2);
  } else if (priv->current_page == 2) {
    GisUbuntuProPage3 *page3 = GIS_UBUNTUPRO_PAGE3(priv->page3);
    display_ua_services(gis_ubuntupro_page3_get_instance_private (page3));
    display_checkmark(gis_ubuntupro_page3_get_instance_private (page3));
    gtk_stack_set_visible_child (GTK_STACK (priv->stack), priv->page3);
    gis_page_apply_complete (GIS_PAGE (page), FALSE);
  } else {
    return FALSE;
  }
  priv->current_page++;
  return TRUE;
}

static void
gis_ubuntupro_page_class_init (GisUbuntuProPageClass *klass)
{
  GisPageClass *page_class = GIS_PAGE_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  gtk_widget_class_set_template_from_resource (GTK_WIDGET_CLASS (klass), "/org/gnome/initial-setup/gis-ubuntupro-page.ui");

  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisUbuntuProPage, page1);
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisUbuntuProPage, page2);
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisUbuntuProPage, page3);
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisUbuntuProPage, stack);
  gtk_widget_class_bind_template_callback (GTK_WIDGET_CLASS (klass), request_token_attach);
  gtk_widget_class_bind_template_callback (GTK_WIDGET_CLASS (klass), request_magic_attach);
  gtk_widget_class_bind_template_callback (GTK_WIDGET_CLASS (klass), on_token_toggled);
  gtk_widget_class_bind_template_callback (GTK_WIDGET_CLASS (klass), on_magic_toggled);

  page_class->page_id = PAGE_ID;
  page_class->locale_changed = gis_ubuntupro_page_locale_changed;
  page_class->apply = gis_ubuntupro_page_apply;
  object_class->constructed = gis_ubuntupro_page_constructed;
}
static void
gis_ubuntupro_page1_class_init (GisUbuntuProPage1Class *klass)
{
  gtk_widget_class_set_template_from_resource (GTK_WIDGET_CLASS (klass), "/org/gnome/initial-setup/gis-ubuntupro-page-1.ui");

  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisUbuntuProPage1, enable_pro_select);
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisUbuntuProPage1, skip_pro_select);
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisUbuntuProPage1, offline_warning);
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisUbuntuProPage1, pro_status_image);
}

static void
gis_ubuntupro_page2_class_init (GisUbuntuProPage2Class *klass)
{
  gtk_widget_class_set_template_from_resource (GTK_WIDGET_CLASS (klass), "/org/gnome/initial-setup/gis-ubuntupro-page-2.ui");

  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisUbuntuProPage2, pin_label);
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisUbuntuProPage2, token_field);
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisUbuntuProPage2, token_status);
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisUbuntuProPage2, token_status_icon);
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisUbuntuProPage2, pin_status);
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisUbuntuProPage2, pin_status_icon);
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisUbuntuProPage2, pin_hint);
}
static void
gis_ubuntupro_page3_class_init (GisUbuntuProPage3Class *klass)
{
  gtk_widget_class_set_template_from_resource (GTK_WIDGET_CLASS (klass), "/org/gnome/initial-setup/gis-ubuntupro-page-3.ui");

  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisUbuntuProPage3, enabled_services);
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisUbuntuProPage3, enabled_services_header);
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisUbuntuProPage3, available_services);
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisUbuntuProPage3, available_services_header);
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisUbuntuProPage3, contract_name);
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisUbuntuProPage3, checkmark);
}

static void
gis_ubuntupro_page_init (GisUbuntuProPage *page)
{
  g_resources_register (ubuntupro_get_resource ());

  // Magic that makes stuff compile
  gis_ubuntupro_page1_get_type ();
  gis_ubuntupro_page2_get_type ();
  gis_ubuntupro_page3_get_type ();

  gtk_widget_init_template (GTK_WIDGET (page));
}
static void
gis_ubuntupro_page1_init (GisUbuntuProPage1 *page)
{
  g_resources_register (ubuntupro_get_resource ());

  gtk_widget_init_template (GTK_WIDGET (page));
}
static void
gis_ubuntupro_page2_init (GisUbuntuProPage2 *page)
{
  g_resources_register (ubuntupro_get_resource ());

  gtk_widget_init_template (GTK_WIDGET (page));
}
static void
gis_ubuntupro_page3_init (GisUbuntuProPage3 *page)
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
