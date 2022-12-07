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

/* Get more apps page {{{1 */

#define PAGE_ID "apps"

#include "config.h"
#include "gis-apps-page.h"
#include "apps-resources.h"

#include <glib/gi18n.h>
#include <gio/gio.h>
#include <snapd-glib/snapd-glib.h>
#include <libsoup/soup.h>

struct _GisAppsPagePrivate {
  SoupSession *soup_session;
  SnapdClient *client;

  GtkWidget *main_stack;
  GtkWidget *spinner;
  GtkWidget *featured_stack;
  GtkWidget *prev_featured_button;
  GtkWidget *next_featured_button;

  GCancellable *cancellable;
  guint featured_snaps_timeout_id;

  GPtrArray *installed_snaps;
  GPtrArray *featured_snaps;
};
typedef struct _GisAppsPagePrivate GisAppsPagePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (GisAppsPage, gis_apps_page, GIS_TYPE_PAGE);

struct _GisSnapTile
{
  GtkFlowBoxChild parent;
  SnapdSnap *snap;
  SoupSession *soup_session;
  GtkWidget *icon;
};
G_DECLARE_FINAL_TYPE (GisSnapTile, gis_snap_tile, GIS, SNAP_TILE, GtkFlowBoxChild)
G_DEFINE_TYPE (GisSnapTile, gis_snap_tile, GTK_TYPE_FLOW_BOX_CHILD)

static void
gis_snap_tile_init (GisSnapTile *item)
{
}

static void
gis_snap_tile_finalize (GObject *object)
{
  g_clear_object (&GIS_SNAP_TILE (object)->snap);
  g_clear_object (&GIS_SNAP_TILE (object)->soup_session);
  G_OBJECT_CLASS (gis_snap_tile_parent_class)->finalize (object);
}

static void
gis_snap_tile_class_init (GisSnapTileClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = gis_snap_tile_finalize;
}

static void
gis_apps_page_constructed (GObject *object)
{
  GisAppsPage *page = GIS_APPS_PAGE (object);

  G_OBJECT_CLASS (gis_apps_page_parent_class)->constructed (object);

  gis_page_set_skippable (GIS_PAGE (page), TRUE);

  gis_page_set_complete (GIS_PAGE (page), TRUE);
  gtk_widget_show (GTK_WIDGET (page));
}

static void
gis_apps_page_dispose (GObject *object)
{
  GisAppsPage *page = GIS_APPS_PAGE (object);
  GisAppsPagePrivate *priv = gis_apps_page_get_instance_private (page);

  g_clear_object (&priv->client);
  g_clear_object (&priv->soup_session);
  g_clear_pointer (&priv->installed_snaps, g_ptr_array_unref);
  g_clear_pointer (&priv->featured_snaps, g_ptr_array_unref);

  if (priv->cancellable != NULL) {
    g_cancellable_cancel (priv->cancellable);
    g_clear_object (&priv->cancellable);
  }

  if (priv->featured_snaps_timeout_id != 0) {
    g_source_remove (priv->featured_snaps_timeout_id);
    priv->featured_snaps_timeout_id = 0;
  }

  G_OBJECT_CLASS (gis_apps_page_parent_class)->dispose (object);
}

static void
open_software (GtkButton      *button,
               const gchar    *uri,
               GisAppsPage *page)
{
  g_autofree gchar *command = NULL;
  g_autoptr(GAppInfo) info = NULL;
  g_autoptr(GError) error = NULL;

  g_autofree gchar *storecmd = NULL;

  storecmd = g_find_program_in_path ("snap-store.ubuntu-software");
  if (storecmd == NULL)
    storecmd = g_find_program_in_path ("gnome-software");
  if (storecmd == NULL) {
    g_warning ("Failed to find snap-store or gnome-software");
    return;
  }

  info = g_app_info_create_from_commandline (storecmd, NULL, G_APP_INFO_CREATE_NONE, &error);
  if (info == NULL) {
     g_warning ("Failed to get launch information from gnome-software: %s", error->message);
     return;
  }
  if (!g_app_info_launch (info, NULL, NULL, &error)) {
     g_warning ("Failed to launch gnome-software: %s", error->message);
     return;
  }
}

static gboolean
gis_apps_page_apply (GisPage *page, GCancellable *cancellable)
{
  gis_ensure_stamp_files (GIS_PAGE (page)->driver);
  gis_driver_quit (GIS_PAGE (page)->driver);
  return FALSE;
}

static void
gis_apps_page_locale_changed (GisPage *page)
{
  gis_page_set_title (GIS_PAGE (page), _("Ready to go"));
}

static void
next_page (GtkWidget *stack, GtkWidget *prev_button, GtkWidget *next_button)
{
  GtkWidget *visible_child;
  GList *children, *link;

  visible_child = gtk_stack_get_visible_child (GTK_STACK (stack));
  children = gtk_container_get_children (GTK_CONTAINER (stack));
  for (link = children; link != NULL; link = link->next) {
    GtkWidget *child = link->data;
    if (child == visible_child) {
      if (link->next == NULL)
        return;

      link = link->next;
      gtk_stack_set_transition_type (GTK_STACK (stack), GTK_STACK_TRANSITION_TYPE_SLIDE_LEFT);
      gtk_stack_set_visible_child (GTK_STACK (stack), link->data);
      gtk_widget_set_sensitive (prev_button, TRUE);
      gtk_widget_set_sensitive (next_button, link->next != NULL);
      return;
    }
  }
}

static void
prev_page (GtkWidget *stack, GtkWidget *prev_button, GtkWidget *next_button)
{
  GtkWidget *visible_child;
  GList *children, *link;

  visible_child = gtk_stack_get_visible_child (GTK_STACK (stack));
  children = gtk_container_get_children (GTK_CONTAINER (stack));
  for (link = children; link != NULL; link = link->next) {
    GtkWidget *child = link->data;
    if (child == visible_child) {
      if (link->prev == NULL)
        return;

      link = link->prev;
      gtk_stack_set_transition_type (GTK_STACK (stack), GTK_STACK_TRANSITION_TYPE_SLIDE_RIGHT);
      gtk_stack_set_visible_child (GTK_STACK (stack), link->data);
      gtk_widget_set_sensitive (prev_button, link->prev != NULL);
      gtk_widget_set_sensitive (next_button, TRUE);
      return;
    }
  }
}

static void
prev_featured (GtkButton   *button,
               GisAppsPage *page)
{
  GisAppsPagePrivate *priv = gis_apps_page_get_instance_private (page);
  prev_page (priv->featured_stack, priv->prev_featured_button, priv->next_featured_button);
}

static void
next_featured (GtkButton   *button,
               GisAppsPage *page)
{
  GisAppsPagePrivate *priv = gis_apps_page_get_instance_private (page);
  next_page (priv->featured_stack, priv->prev_featured_button, priv->next_featured_button);
}

static void
gis_apps_page_class_init (GisAppsPageClass *klass)
{
  GisPageClass *page_class = GIS_PAGE_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  gtk_widget_class_set_template_from_resource (GTK_WIDGET_CLASS (klass), "/org/gnome/initial-setup/gis-apps-page.ui");
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisAppsPage, main_stack);
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisAppsPage, spinner);
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisAppsPage, featured_stack);
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisAppsPage, prev_featured_button);
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisAppsPage, next_featured_button);
  gtk_widget_class_bind_template_callback (GTK_WIDGET_CLASS (klass), open_software);
  gtk_widget_class_bind_template_callback (GTK_WIDGET_CLASS (klass), prev_featured);
  gtk_widget_class_bind_template_callback (GTK_WIDGET_CLASS (klass), next_featured);

  page_class->page_id = PAGE_ID;
  page_class->apply = gis_apps_page_apply;
  page_class->locale_changed = gis_apps_page_locale_changed;
  object_class->constructed = gis_apps_page_constructed;
  object_class->dispose = gis_apps_page_dispose;
}

static GdkPixbuf *
load_snap_icon (SnapdSnap *snap)
{
  const gchar *url;
  g_autoptr(SnapdClient) client = NULL;
  g_autoptr(SnapdIcon) icon = NULL;
  g_autoptr(GInputStream) input_stream = NULL;
  g_autoptr(GdkPixbuf) pixbuf = NULL;
  g_autoptr(GError) error = NULL;

  url = snapd_snap_get_icon (snap);
  if (url == NULL || !g_str_has_prefix (url, "/v2/icons/"))
    return NULL;

  client = snapd_client_new ();
  icon = snapd_client_get_icon_sync (client, snapd_snap_get_name (snap), NULL, &error);
  if (icon == NULL) {
     g_warning ("Failed to get snap icon: %s", error->message);
     return NULL;
  }
  input_stream = g_memory_input_stream_new_from_bytes (snapd_icon_get_data (icon));
  pixbuf = gdk_pixbuf_new_from_stream_at_scale (input_stream, 64, 64, TRUE, NULL, &error);
  if (pixbuf == NULL) {
     g_warning ("Failed to decode snap icon: %s", error->message);
     return NULL;
  }

  return g_steal_pointer (&pixbuf);
}

static GdkPixbuf *
load_local_icon (const gchar *name, GError **error)
{
  if (g_str_has_prefix (name, "/"))
    return gdk_pixbuf_new_from_file_at_scale (name, 64, 64, TRUE, error);
  else {
    g_autoptr(GdkPixbuf) pixbuf = NULL;

    pixbuf = gtk_icon_theme_load_icon (gtk_icon_theme_get_default (), name, 64, 0, error);
    if (pixbuf == NULL)
      return NULL;

    if (gdk_pixbuf_get_width (pixbuf) == 64 && gdk_pixbuf_get_height (pixbuf) == 64)
      return g_steal_pointer (&pixbuf);

    return gdk_pixbuf_scale_simple (pixbuf, 64, 64, GDK_INTERP_BILINEAR);
  }
}

static GdkPixbuf *
load_desktop_icon (SnapdSnap *snap)
{
  GPtrArray *apps;
  guint i;

  apps = snapd_snap_get_apps (snap);
  for (i = 0; i < apps->len; i++) {
    SnapdApp *app = g_ptr_array_index (apps, i);
    const gchar *desktop_file_path;
    g_autoptr(GKeyFile) desktop_file = NULL;
    g_autofree gchar *icon = NULL;
    g_autoptr(GdkPixbuf) pixbuf = NULL;
    g_autoptr(GError) error = NULL;

    desktop_file_path = snapd_app_get_desktop_file (app);
    if (desktop_file_path == NULL)
      continue;

    desktop_file = g_key_file_new ();
    if (!g_key_file_load_from_file (desktop_file, desktop_file_path, G_KEY_FILE_NONE, &error)) {
      g_warning ("Failed to load desktop file %s: %s", desktop_file_path, error->message);
      continue;
    }

    icon = g_key_file_get_string (desktop_file, G_KEY_FILE_DESKTOP_GROUP, G_KEY_FILE_DESKTOP_KEY_ICON, &error);
    if (icon == NULL) {
      g_warning ("Failed to get desktop file icon %s: %s", desktop_file_path, error->message);
      continue;
    }

    pixbuf = load_local_icon (icon, &error);
    if (pixbuf == NULL) {
      g_warning ("Failed to load icon %s: %s", icon, error->message);
      continue;
    }

    return g_steal_pointer (&pixbuf);
  }

  return NULL;
}

static void
icon_cb (GObject *object, GAsyncResult *result, gpointer user_data)
{
  GisSnapTile *tile = user_data;
  g_autoptr(GInputStream) stream = NULL;
  g_autoptr(GdkPixbuf) pixbuf = NULL;
  g_autoptr(GError) error = NULL;

  stream = soup_session_send_finish (SOUP_SESSION (object), result, &error);
  if (stream == NULL) {
    g_warning ("Failed to download icon: %s", error->message);
    return;
  }

  pixbuf = gdk_pixbuf_new_from_stream_at_scale (stream, 64, 64, TRUE, NULL, &error);
  if (pixbuf == NULL) {
    g_warning ("Failed to load icon: %s", error->message);
    return;
  }

  gtk_image_set_from_pixbuf (GTK_IMAGE (tile->icon), pixbuf);
}

static void
load_store_icon (GisSnapTile *tile)
{
  const gchar *url;
  g_autoptr(SoupMessage) message = NULL;

  url = snapd_snap_get_icon (tile->snap);
  if (url == NULL || !(g_str_has_prefix (url, "http://") || g_str_has_prefix (url, "https://")))
    return;

  message = soup_message_new ("GET", url);
  soup_session_send_async (tile->soup_session, message, 0, NULL, icon_cb, tile);
}

static void
load_icon (GisSnapTile *tile)
{
  g_autoptr(GdkPixbuf) pixbuf = NULL;
  g_autoptr(GError) error = NULL;

  pixbuf = load_snap_icon (tile->snap);
  if (pixbuf != NULL) {
    gtk_image_set_from_pixbuf (GTK_IMAGE (tile->icon), pixbuf);
    return;
  }

  pixbuf = load_desktop_icon (tile->snap);
  if (pixbuf != NULL) {
    gtk_image_set_from_pixbuf (GTK_IMAGE (tile->icon), pixbuf);
    return;
  }

  /* Add placeholder icon */
  pixbuf = gdk_pixbuf_new_from_resource_at_scale ("/org/gnome/initial-setup/default-snap-icon.svg", 64, 64, TRUE, &error);
  if (pixbuf != NULL)
    gtk_image_set_from_pixbuf (GTK_IMAGE (tile->icon), pixbuf);

  load_store_icon (tile);
}

static GtkWidget *
gis_snap_tile_new (SnapdSnap *snap, SoupSession *soup_session)
{
  const gchar *title;
  GisSnapTile *tile;
  GtkWidget *box, *label;

  tile = g_object_new (gis_snap_tile_get_type (), NULL);
  tile->snap = g_object_ref (snap);
  tile->soup_session = g_object_ref (soup_session);

  title = snapd_snap_get_title (snap);
  if (title == NULL)
    title = snapd_snap_get_name (snap);

  box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
  gtk_widget_show (box);
  gtk_container_add (GTK_CONTAINER (tile), box);

  tile->icon = gtk_image_new ();
  gtk_widget_show (tile->icon);
  gtk_box_pack_start (GTK_BOX (box), tile->icon, FALSE, TRUE, 0);

  label = gtk_label_new (title);
  gtk_widget_show (label);
  gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
  gtk_box_pack_start (GTK_BOX (box), label, TRUE, TRUE, 0);

  load_icon (tile);

  return GTK_WIDGET (tile);
}

static gboolean
contains_snap (GPtrArray *snaps, const gchar *name)
{
  guint i;

  for (i = 0; i < snaps->len; i++) {
    SnapdSnap *snap = g_ptr_array_index (snaps, i);
    if (g_strcmp0 (snapd_snap_get_name (snap), name) == 0)
      return TRUE;
  }

  return FALSE;
}

static void
on_app_clicked (GtkWidget *flow_box, GtkFlowBoxChild *child, gpointer user_data)
{
  GisSnapTile *tile = NULL;
  g_autofree gchar *url = NULL;
  g_autoptr (GError) error = NULL;

  if (!GIS_IS_SNAP_TILE (child))
      return;

  tile = GIS_SNAP_TILE (child);

  url = g_strdup_printf ("snap://%s", snapd_snap_get_name (tile->snap));
  if (!g_app_info_launch_default_for_uri (url, NULL, &error))
     g_warning ("Failed to open %s: %s", url, error->message);
}

static void
add_tile (GtkWidget *stack, GtkWidget *tile)
{
  GList *children;
  GtkWidget *flow_box = NULL;
  gint count = 0;

  children = gtk_container_get_children (GTK_CONTAINER (stack));
  if (children != NULL) {
    flow_box = g_list_last (children)->data;
    count = g_list_length (gtk_container_get_children (GTK_CONTAINER (flow_box)));
  }
  if (flow_box == NULL || count >= 15) {
    count = 0;
    flow_box = gtk_flow_box_new ();
    gtk_flow_box_set_min_children_per_line (GTK_FLOW_BOX (flow_box), 5);
    gtk_flow_box_set_max_children_per_line (GTK_FLOW_BOX (flow_box), 5);
    gtk_flow_box_set_selection_mode (GTK_FLOW_BOX (flow_box), GTK_SELECTION_NONE);
    g_signal_connect (flow_box, "child-activated",
                      G_CALLBACK (on_app_clicked), NULL);
    gtk_widget_show (flow_box);
    gtk_flow_box_set_row_spacing (GTK_FLOW_BOX (flow_box), 10);
    gtk_flow_box_set_column_spacing (GTK_FLOW_BOX (flow_box), 10);
    gtk_container_add (GTK_CONTAINER (stack), flow_box);
  }

  gtk_container_add (GTK_CONTAINER (flow_box), tile);
}

static void
gis_app_page_populate_featured_snaps (GisAppsPage *page)
{
    GisAppsPagePrivate *priv = gis_apps_page_get_instance_private (page);
    GtkSizeGroup *size_group;
    guint i, n_tiles = 0, n_extra = 0;

    size_group = gtk_size_group_new (GTK_SIZE_GROUP_BOTH);

    for (i = 0; i < priv->featured_snaps->len; i++) {
      SnapdSnap *snap = g_ptr_array_index (priv->featured_snaps, i);
      GtkWidget *tile;

      /* Skip if already installed */
      if (priv->installed_snaps != NULL && contains_snap (priv->installed_snaps, snapd_snap_get_name (snap)))
        continue;

      /* Ignore common snaps that have default .debs */
      if (g_strcmp0 (snapd_snap_get_name (snap), "firefox") == 0)
        continue;

      tile = gis_snap_tile_new (snap, priv->soup_session);
      gtk_widget_show (tile);
      gtk_size_group_add_widget (size_group, tile);

      add_tile (priv->featured_stack, tile);
      n_tiles += 1;
    }

    /* Add spacers to fill up page */
    if (n_tiles % 15 != 0)
      n_extra = 15 - n_tiles % 15;
    for (i = 0; i < n_extra; i++) {
      GtkWidget *spacer = gtk_label_new ("");
      gtk_widget_show (spacer);
      gtk_size_group_add_widget (size_group, spacer);
      add_tile (priv->featured_stack, spacer);
    }

    gtk_widget_set_visible (priv->next_featured_button, g_list_length (gtk_container_get_children (GTK_CONTAINER (priv->featured_stack))) > 1);
    gtk_widget_set_visible (priv->prev_featured_button, g_list_length (gtk_container_get_children (GTK_CONTAINER (priv->featured_stack))) > 1);
}

static void
gis_apps_page_try_to_get_featured_snaps (GisAppsPage *page);

static void
gis_app_page_on_featured_snaps_ready (GObject *source_object,
                                      GAsyncResult *res,
                                      gpointer user_data)
{
  SnapdClient *client = SNAPD_CLIENT (source_object);
  g_autoptr(GPtrArray) featured_snaps = NULL;
  g_autoptr(GError) error = NULL;

  featured_snaps = snapd_client_find_section_finish (client, res, NULL, &error);

  if (featured_snaps == NULL) {
    if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
      g_warning ("Failed to get featured snaps: %s", error->message);
      /* On a fresh boot interacting with snapd can fail (see LP: #1824188).
       * In case of error, try to get the featured snaps again.*/
      gis_apps_page_try_to_get_featured_snaps (GIS_APPS_PAGE (user_data));
    }
  } else {
    GisAppsPage *page = GIS_APPS_PAGE (user_data);
    GisAppsPagePrivate *priv = gis_apps_page_get_instance_private (page);

    priv->featured_snaps = g_steal_pointer (&featured_snaps);

    gtk_spinner_stop (GTK_SPINNER (priv->spinner));
    gtk_stack_set_visible_child_name (GTK_STACK (priv->main_stack), "page_snaps");

    gis_app_page_populate_featured_snaps (page);
  }
}

static gboolean
gis_app_page_get_featured_snaps_timeout (gpointer user_data)
{
  GisAppsPage *page = GIS_APPS_PAGE (user_data);
  GisAppsPagePrivate *priv = gis_apps_page_get_instance_private (page);

  priv->featured_snaps_timeout_id = 0;

  snapd_client_find_section_async (priv->client,
                                   SNAPD_FIND_FLAGS_NONE,
                                   "ubuntu-firstrun",
                                   NULL,
                                   priv->cancellable,
                                   gis_app_page_on_featured_snaps_ready,
                                   page);

  return G_SOURCE_REMOVE;
}

void
gis_apps_page_try_to_get_featured_snaps (GisAppsPage *page)
{
  GisAppsPagePrivate *priv = gis_apps_page_get_instance_private (page);

  g_return_if_fail (priv->featured_snaps == NULL);

  if (priv->featured_snaps_timeout_id == 0)
    priv->featured_snaps_timeout_id = g_timeout_add_seconds (1, gis_app_page_get_featured_snaps_timeout, page);
}

void
gis_app_page_on_installed_snaps_ready (GObject *source_object,
                                       GAsyncResult *res,
                                       gpointer user_data)
{
  GisAppsPage *page;
  GisAppsPagePrivate *priv;
  SnapdClient *client = SNAPD_CLIENT (source_object);
  g_autoptr(GPtrArray) installed_snaps = NULL;
  g_autoptr(GError) error = NULL;

  installed_snaps = snapd_client_get_snaps_finish (client, res, &error);
  if (installed_snaps == NULL) {
    if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
      /* Discard errors in getting the list of installed snaps. Print out a
       * warning here and avoid to use installed_snaps to filter the featured
       * snaps. */
      g_warning ("Failed to get installed snaps: %s", error->message);
    } else {
      return;
    }
  }

  page = GIS_APPS_PAGE (user_data);
  priv = gis_apps_page_get_instance_private (page);

  priv->installed_snaps = g_steal_pointer (&installed_snaps);
  gis_apps_page_try_to_get_featured_snaps (page);
}

static void
gis_apps_page_init (GisAppsPage *page)
{
  GisAppsPagePrivate *priv = gis_apps_page_get_instance_private (page);

  g_resources_register (apps_get_resource ());

  gtk_widget_init_template (GTK_WIDGET (page));

  priv->soup_session = soup_session_new ();
  priv->client = snapd_client_new ();
  priv->cancellable = g_cancellable_new ();

  gtk_stack_set_visible_child_name (GTK_STACK (priv->main_stack), "page_spinner");
  gtk_spinner_start (GTK_SPINNER (priv->spinner));

  snapd_client_get_snaps_async (priv->client,
                                SNAPD_GET_SNAPS_FLAGS_NONE,
                                NULL,
                                priv->cancellable,
                                gis_app_page_on_installed_snaps_ready,
                                page);
}

GisPage *
gis_prepare_apps_page (GisDriver *driver)
{
  return g_object_new (GIS_TYPE_APPS_PAGE,
                       "driver", driver,
                       NULL);
}
