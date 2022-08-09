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

#ifndef __GIS_UBUNTUPRO_PAGE_H__
#define __GIS_UBUNTUPRO_PAGE_H__

#include "gnome-initial-setup.h"

G_BEGIN_DECLS

#define GIS_TYPE_UBUNTUPRO_PAGE            (gis_ubuntupro_page_get_type ())
#define GIS_TYPE_UBUNTUPRO_PAGE1           (gis_ubuntupro_page1_get_type ())
#define GIS_TYPE_UBUNTUPRO_PAGE2           (gis_ubuntupro_page2_get_type ())
#define GIS_TYPE_UBUNTUPRO_PAGE3           (gis_ubuntupro_page3_get_type ())
#define GIS_UBUNTUPRO_PAGE(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), GIS_TYPE_UBUNTUPRO_PAGE, GisUbuntuProPage))
#define GIS_UBUNTUPRO_PAGE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  GIS_TYPE_UBUNTUPRO_PAGE, GisUbuntuProPageClass))
#define GIS_UBUNTUPRO_PAGE1(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), GIS_TYPE_UBUNTUPRO_PAGE1, GisUbuntuProPage1))
#define GIS_UBUNTUPRO_PAGE1_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  GIS_TYPE_UBUNTUPRO_PAGE1, GisUbuntuProPage1Class))
#define GIS_UBUNTUPRO_PAGE2(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), GIS_TYPE_UBUNTUPRO_PAGE2, GisUbuntuProPage2))
#define GIS_UBUNTUPRO_PAGE2_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  GIS_TYPE_UBUNTUPRO_PAGE2, GisUbuntuProPage2Class))
#define GIS_UBUNTUPRO_PAGE3(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), GIS_TYPE_UBUNTUPRO_PAGE3, GisUbuntuProPage3))
#define GIS_UBUNTUPRO_PAGE3_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  GIS_TYPE_UBUNTUPRO_PAGE3, GisUbuntuProPage3Class))
#define GIS_IS_UBUNTUPRO_PAGE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GIS_TYPE_UBUNTUPRO_PAGE))
#define GIS_IS_UBUNTUPRO_PAGE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  GIS_TYPE_UBUNTUPRO_PAGE))
#define GIS_UBUNTUPRO_PAGE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  GIS_TYPE_UBUNTUPRO_PAGE, GisUbuntuProPageClass))
#define GIS_UBUNTUPRO_PAGE1_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  GIS_TYPE_UBUNTUPRO_PAGE1, GisUbuntuProPage1Class))
#define GIS_UBUNTUPRO_PAGE2_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  GIS_TYPE_UBUNTUPRO_PAGE2, GisUbuntuProPage2Class))
#define GIS_UBUNTUPRO_PAGE3_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  GIS_TYPE_UBUNTUPRO_PAGE3, GisUbuntuProPage3Class))

typedef struct _GisUbuntuProPage        GisUbuntuProPage;
typedef struct _GisUbuntuProPageClass   GisUbuntuProPageClass;
typedef struct _GisUbuntuProPage1       GisUbuntuProPage1;
typedef struct _GisUbuntuProPage1Class  GisUbuntuProPage1Class;
typedef struct _GisUbuntuProPage2       GisUbuntuProPage2;
typedef struct _GisUbuntuProPage2Class  GisUbuntuProPage2Class;
typedef struct _GisUbuntuProPage3       GisUbuntuProPage3;
typedef struct _GisUbuntuProPage3Class  GisUbuntuProPage3Class;
typedef struct _RestJSONResponse        RestJSONResponse;

struct _GisUbuntuProPage
{
  GisPage parent;
};

struct _GisUbuntuProPageClass
{
  GisPageClass parent_class;
};
struct _GisUbuntuProPage1
{
  GisPage parent;
};

struct _GisUbuntuProPage1Class
{
  GisPageClass parent_class;
};
struct _GisUbuntuProPage2
{
  GisPage parent;
};

struct _GisUbuntuProPage2Class
{
  GisPageClass parent_class;
};
struct _GisUbuntuProPage3
{
  GisPage parent;
};

struct _GisUbuntuProPage3Class
{
  GisPageClass parent_class;
};

struct _RestJSONResponse {
    gint64 expiresIn;
    gchar *token;
    gchar *code;
    gchar *contractID;
    gchar *contractToken;
};

GType gis_ubuntupro_page_get_type (void);
GType gis_ubuntupro_page1_get_type (void);
GType gis_ubuntupro_page2_get_type (void);
GType gis_ubuntupro_page3_get_type (void);

GisPage *gis_prepare_ubuntu_pro_page (GisDriver *driver);

G_END_DECLS

#endif /* __GIS_UBUNTUPRO_PAGE_H__ */
