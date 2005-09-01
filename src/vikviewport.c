/*
 * viking -- GPS Data and Topo Analyzer, Explorer, and Manager
 *
 * Copyright (C) 2003-2005, Evan Battaglia <gtoevan@gmx.net>
 *
 * Lat/Lon plotting functions calcxy* are from GPSDrive
 * GPSDrive Copyright (C) 2001-2004 Fritz Ganter <ganter@ganter.at>
 *
 * Multiple UTM zone patch by Kit Transue <notlostyet@didactek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#define DEFAULT_BACKGROUND_COLOR "#CCCCCC"

#include <gtk/gtk.h>
#include <math.h>

#include "coords.h"
#include "vikcoord.h"
#include "vikviewport.h"

#include "mapcoord.h"

/* for ALTI_TO_MPP */
#include "globals.h"
#include "googlemaps.h"
#include "khmaps.h"

static gdouble EASTING_OFFSET = 500000.0;

static void viewport_class_init ( VikViewportClass *klass );
static void viewport_init ( VikViewport *vvp );
static void viewport_finalize ( GObject *gob );
static void viewport_utm_zone_check ( VikViewport *vvp );

static gboolean calcxy(double *x, double *y, double lg, double lt, double zero_long, double zero_lat, double pixelfact_x, double pixelfact_y, gint mapSizeX2, gint mapSizeY2 );
static gboolean calcxy_rev(double *lg, double *lt, gint x, gint y, double zero_long, double zero_lat, double pixelfact_x, double pixelfact_y, gint mapSizeX2, gint mapSizeY2 );
double calcR (double lat);

static double Radius[181];
static void viewport_init_ra();

static GObjectClass *parent_class;

static void viewport_google_rezoom ( VikViewport *vvp );


struct _VikViewport {
  GtkDrawingArea drawing_area;
  GdkPixmap *scr_buffer;
  gint width, height;
  VikCoord center;
  VikCoordMode coord_mode;
  gdouble xmpp, ympp;

  GdkPixbuf *alpha_pixbuf;
  guint8 alpha_pixbuf_width;
  guint8 alpha_pixbuf_height;

  gdouble utm_zone_width;
  gboolean one_utm_zone;

  GdkGC *background_gc;
  GdkColor background_color;

  /* subset of coord types. lat lon can be plotted in 2 ways, google or exp. */
  VikViewportDrawMode drawmode;

  /* handy conversion factors which make google plotting extremely fast */
  gdouble google_calcx_fact;
  gdouble google_calcy_fact;
  gdouble google_calcx_rev_fact;
  gdouble google_calcy_rev_fact;
};

static gdouble
viewport_utm_zone_width ( VikViewport *vvp )
{
  if ( vvp->coord_mode == VIK_COORD_UTM ) {
    struct LatLon ll;

    /* get latitude of screen bottom */
    struct UTM utm = *((struct UTM *)(vik_viewport_get_center ( vvp )));
    utm.northing -= vvp -> height * vvp -> ympp / 2;
    a_coords_utm_to_latlon ( &utm, &ll );

    /* boundary */
    ll.lon = (utm.zone - 1) * 6 - 180 ;
    a_coords_latlon_to_utm ( &ll, &utm);
    return fabs ( utm.easting - EASTING_OFFSET ) * 2;
  } else
    return 0.0;
}


GType vik_viewport_get_type (void)
{
  static GType vvp_type = 0;

  if (!vvp_type)
  {
    static const GTypeInfo vvp_info = 
    {
      sizeof (VikViewportClass),
      NULL, /* base_init */
      NULL, /* base_finalize */
      (GClassInitFunc) viewport_class_init,
      NULL, /* class_finalize */
      NULL, /* class_data */
      sizeof (VikViewport),
      0,
      (GInstanceInitFunc) viewport_init,
    };
    vvp_type = g_type_register_static ( GTK_TYPE_DRAWING_AREA, "VikViewport", &vvp_info, 0 );
  }
  return vvp_type;
}

static void viewport_class_init ( VikViewportClass *klass )
{
  /* Destructor */
  GObjectClass *object_class;

  object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = viewport_finalize;

  parent_class = g_type_class_peek_parent (klass);
}

VikViewport *vik_viewport_new ()
{
  return VIK_VIEWPORT ( g_object_new ( VIK_VIEWPORT_TYPE, NULL ) );
}

static void viewport_init ( VikViewport *vvp )
{
  viewport_init_ra();

  /* TODO: not static */
  vvp->xmpp = 4.0;
  vvp->ympp = 4.0;
  vvp->coord_mode = VIK_COORD_UTM;
  vvp->drawmode = VIK_VIEWPORT_DRAWMODE_UTM;
  vvp->center.north_south = 0;
  vvp->center.east_west = -166021;
  vvp->center.utm_zone = 31;
  vvp->center.utm_letter = 'N';
  vvp->scr_buffer = NULL;
  vvp->alpha_pixbuf = NULL;
  vvp->alpha_pixbuf_width = vvp->alpha_pixbuf_height = 0;
  vvp->utm_zone_width = 0.0;
  vvp->background_gc = NULL;
  g_signal_connect (G_OBJECT(vvp), "configure_event", G_CALLBACK(vik_viewport_configure), NULL);
}

GdkColor *vik_viewport_get_background_gdkcolor ( VikViewport *vvp )
{
  GdkColor *rv = g_malloc ( sizeof ( GdkColor ) );
  *rv = vvp->background_color;
  return rv;
}

/* returns pointer to internal static storage, changes next time function called, use quickly */
const gchar *vik_viewport_get_background_color ( VikViewport *vvp )
{
  static gchar color[8];
  g_snprintf(color, sizeof(color), "#%.2x%.2x%.2x", (int)(vvp->background_color.red/256),(int)(vvp->background_color.green/256),(int)(vvp->background_color.blue/256));
  return color;
}

void vik_viewport_set_background_color ( VikViewport *vvp, const gchar *colorname )
{
  g_assert ( vvp->background_gc );
  gdk_color_parse ( colorname, &(vvp->background_color) );
  gdk_gc_set_rgb_fg_color ( vvp->background_gc, &(vvp->background_color) );
}

void vik_viewport_set_background_gdkcolor ( VikViewport *vvp, GdkColor *color )
{
  g_assert ( vvp->background_gc );
  vvp->background_color = *color;
  gdk_gc_set_rgb_fg_color ( vvp->background_gc, color );
}


GdkGC *vik_viewport_new_gc ( VikViewport *vvp, const gchar *colorname, gint thickness )
{
  GdkGC *rv;
  GdkColor color;

  rv = gdk_gc_new ( GTK_WIDGET(vvp)->window );
  gdk_color_parse ( colorname, &color );
  gdk_gc_set_rgb_fg_color ( rv, &color );
  gdk_gc_set_line_attributes ( rv, thickness, GDK_LINE_SOLID, GDK_CAP_ROUND, GDK_JOIN_ROUND );
  return rv;
}

GdkGC *vik_viewport_new_gc_from_color ( VikViewport *vvp, GdkColor *color, gint thickness )
{
  GdkGC *rv;

  rv = gdk_gc_new ( GTK_WIDGET(vvp)->window );
  gdk_gc_set_rgb_fg_color ( rv, color );
  gdk_gc_set_line_attributes ( rv, thickness, GDK_LINE_SOLID, GDK_CAP_ROUND, GDK_JOIN_ROUND );
  return rv;
}

void vik_viewport_configure_manually ( VikViewport *vvp, gint width, guint height )
{
  vvp->width = width;
  vvp->height = height;
  if ( vvp->scr_buffer )
    g_object_unref ( G_OBJECT ( vvp->scr_buffer ) );
  vvp->scr_buffer = gdk_pixmap_new ( GTK_WIDGET(vvp)->window, vvp->width, vvp->height, -1 );
}


GdkPixmap *vik_viewport_get_pixmap ( VikViewport *vvp )
{
  return vvp->scr_buffer;
}

gboolean vik_viewport_configure ( VikViewport *vvp )
{
  g_return_val_if_fail ( vvp != NULL, TRUE );

  vvp->width = GTK_WIDGET(vvp)->allocation.width;
  vvp->height = GTK_WIDGET(vvp)->allocation.height;

  if ( vvp->scr_buffer )
    g_object_unref ( G_OBJECT ( vvp->scr_buffer ) );

  vvp->scr_buffer = gdk_pixmap_new ( GTK_WIDGET(vvp)->window, vvp->width, vvp->height, -1 );

  /* this is down here so it can get a GC (necessary?) */
  if ( ! vvp->background_gc )
  {
    vvp->background_gc = vik_viewport_new_gc ( vvp, "", 1 );
    vik_viewport_set_background_color ( vvp, DEFAULT_BACKGROUND_COLOR ); /* set to "backup" color in vvp->background_color */
  }

  return FALSE;	
}

static void viewport_finalize ( GObject *gob )
{
  VikViewport *vvp = VIK_VIEWPORT(gob);

  g_return_if_fail ( vvp != NULL );

  if ( vvp->scr_buffer )
    g_object_unref ( G_OBJECT ( vvp->scr_buffer ) );

  if ( vvp->alpha_pixbuf )
    g_object_unref ( G_OBJECT ( vvp->alpha_pixbuf ) );

  if ( vvp->background_gc )
    g_object_unref ( G_OBJECT ( vvp->background_gc ) );

  G_OBJECT_CLASS(parent_class)->finalize(gob);
}

void vik_viewport_clear ( VikViewport *vvp )
{
  g_return_if_fail ( vvp != NULL );
  gdk_draw_rectangle(GDK_DRAWABLE(vvp->scr_buffer), vvp->background_gc, TRUE, 0, 0, vvp->width, vvp->height);
}

void vik_viewport_sync ( VikViewport *vvp )
{
  g_return_if_fail ( vvp != NULL );
  gdk_draw_drawable(GTK_WIDGET(vvp)->window, GTK_WIDGET(vvp)->style->bg_gc[0], GDK_DRAWABLE(vvp->scr_buffer), 0, 0, 0, 0, vvp->width, vvp->height);
}

void vik_viewport_pan_sync ( VikViewport *vvp, gint x_off, gint y_off )
{
  g_return_if_fail ( vvp != NULL );
  gdk_draw_drawable(GTK_WIDGET(vvp)->window, GTK_WIDGET(vvp)->style->bg_gc[0], GDK_DRAWABLE(vvp->scr_buffer), 0, 0, x_off, y_off, vvp->width, vvp->height);
}

void vik_viewport_set_zoom ( VikViewport *vvp, gdouble xympp )
{
  g_return_if_fail ( vvp != NULL );
  if ( xympp >= VIK_VIEWPORT_MIN_ZOOM && xympp <= VIK_VIEWPORT_MAX_ZOOM )
    vvp->xmpp = vvp->ympp = xympp;

  if ( vvp->drawmode == VIK_VIEWPORT_DRAWMODE_UTM )
    viewport_utm_zone_check(vvp);
  else if ( vvp->drawmode == VIK_VIEWPORT_DRAWMODE_GOOGLE )
    viewport_google_rezoom ( vvp );
}

/* or could do factor */
void vik_viewport_zoom_in ( VikViewport *vvp )
{
  g_return_if_fail ( vvp != NULL );
  if ( vvp->xmpp >= (VIK_VIEWPORT_MIN_ZOOM*2) && vvp->ympp >= (VIK_VIEWPORT_MIN_ZOOM*2) )
  {
    vvp->xmpp /= 2;
    vvp->ympp /= 2;

    if ( vvp->drawmode == VIK_VIEWPORT_DRAWMODE_GOOGLE )
      viewport_google_rezoom ( vvp );

    viewport_utm_zone_check(vvp);
  }
}

void vik_viewport_zoom_out ( VikViewport *vvp )
{
  g_return_if_fail ( vvp != NULL );
  if ( vvp->xmpp <= (VIK_VIEWPORT_MAX_ZOOM/2) && vvp->ympp <= (VIK_VIEWPORT_MAX_ZOOM/2) )
  {
    vvp->xmpp *= 2;
    vvp->ympp *= 2;

    if ( vvp->drawmode == VIK_VIEWPORT_DRAWMODE_GOOGLE )
      viewport_google_rezoom ( vvp );

    viewport_utm_zone_check(vvp);
  }
}

gdouble vik_viewport_get_zoom ( VikViewport *vvp )
{
  if ( vvp->xmpp == vvp->ympp )
    return vvp->xmpp;
  return 0.0;
}

gdouble vik_viewport_get_xmpp ( VikViewport *vvp )
{
  return vvp->xmpp;
}

gdouble vik_viewport_get_ympp ( VikViewport *vvp )
{
  return vvp->ympp;
}

void vik_viewport_set_xmpp ( VikViewport *vvp, gdouble xmpp )
{
  if ( xmpp >= VIK_VIEWPORT_MIN_ZOOM && xmpp <= VIK_VIEWPORT_MAX_ZOOM ) {
    vvp->xmpp = xmpp;
    if ( vvp->drawmode == VIK_VIEWPORT_DRAWMODE_UTM )
      viewport_utm_zone_check(vvp);
    if ( vvp->drawmode == VIK_VIEWPORT_DRAWMODE_GOOGLE )
      viewport_google_rezoom ( vvp );
  }
}

void vik_viewport_set_ympp ( VikViewport *vvp, gdouble ympp )
{
  if ( ympp >= VIK_VIEWPORT_MIN_ZOOM && ympp <= VIK_VIEWPORT_MAX_ZOOM ) {
    vvp->ympp = ympp;
    if ( vvp->drawmode == VIK_VIEWPORT_DRAWMODE_UTM )
      viewport_utm_zone_check(vvp);
    if ( vvp->drawmode == VIK_VIEWPORT_DRAWMODE_GOOGLE )
      viewport_google_rezoom ( vvp );
  }
}


const VikCoord *vik_viewport_get_center ( VikViewport *vvp )
{
  g_return_val_if_fail ( vvp != NULL, NULL );
  return &(vvp->center);
}

/* called every time we update coordinates/zoom */
static void viewport_utm_zone_check ( VikViewport *vvp )
{
  if ( vvp->coord_mode == VIK_COORD_UTM )
  {
    struct UTM utm;
    struct LatLon ll;
    a_coords_utm_to_latlon ( (struct UTM *) &(vvp->center), &ll );
    a_coords_latlon_to_utm ( &ll, &utm );
    if ( utm.zone != vvp->center.utm_zone )
      *((struct UTM *)(&vvp->center)) = utm;

    /* misc. stuff so we don't have to check later */
    vvp->utm_zone_width = viewport_utm_zone_width ( vvp );
    vvp->one_utm_zone = ( vik_viewport_rightmost_zone(vvp) == vik_viewport_leftmost_zone(vvp) );
  }
}

void vik_viewport_set_center_latlon ( VikViewport *vvp, const struct LatLon *ll )
{
  vik_coord_load_from_latlon ( &(vvp->center), vvp->coord_mode, ll );
  if ( vvp->coord_mode == VIK_COORD_UTM )
    viewport_utm_zone_check ( vvp );
}

void vik_viewport_set_center_utm ( VikViewport *vvp, const struct UTM *utm )
{
  vik_coord_load_from_utm ( &(vvp->center), vvp->coord_mode, utm );
  if ( vvp->coord_mode == VIK_COORD_UTM )
    viewport_utm_zone_check ( vvp );
}

void vik_viewport_set_center_coord ( VikViewport *vvp, const VikCoord *coord )
{
  vvp->center = *coord;
  if ( vvp->coord_mode == VIK_COORD_UTM )
    viewport_utm_zone_check ( vvp );
}

void vik_viewport_corners_for_zonen ( VikViewport *vvp, int zone, VikCoord *ul, VikCoord *br )
{
  g_return_if_fail ( vvp->coord_mode == VIK_COORD_UTM );

  /* get center, then just offset */
  vik_viewport_center_for_zonen ( vvp, VIK_UTM(ul), zone );
  ul->mode = VIK_COORD_UTM;
  *br = *ul;

  ul->north_south += (vvp->ympp * vvp->height / 2);
  ul->east_west -= (vvp->xmpp * vvp->width / 2);
  br->north_south -= (vvp->ympp * vvp->height / 2);
  br->east_west += (vvp->xmpp * vvp->width / 2);
}

void vik_viewport_center_for_zonen ( VikViewport *vvp, struct UTM *center, int zone)
{
  if ( vvp->coord_mode == VIK_COORD_UTM ) {
    *center = *((struct UTM *)(vik_viewport_get_center ( vvp )));
    center->easting -= ( zone - center->zone ) * vvp->utm_zone_width;
    center->zone = zone;
  }
}

gchar vik_viewport_leftmost_zone ( VikViewport *vvp )
{
  if ( vvp->coord_mode == VIK_COORD_UTM ) {
    VikCoord coord;
    g_assert ( vvp != NULL );
    vik_viewport_screen_to_coord ( vvp, 0, 0, &coord );
    return coord.utm_zone;
  }
  return '\0';
}

gchar vik_viewport_rightmost_zone ( VikViewport *vvp )
{
  if ( vvp->coord_mode == VIK_COORD_UTM ) {
    VikCoord coord;
    g_assert ( vvp != NULL );
    vik_viewport_screen_to_coord ( vvp, vvp->width, 0, &coord );
    return coord.utm_zone;
  }
  return '\0';
}


void vik_viewport_set_center_screen ( VikViewport *vvp, int x, int y )
{
  g_return_if_fail ( vvp != NULL );
  if ( vvp->coord_mode == VIK_COORD_UTM ) {
    /* slightly optimized */
    vvp->center.east_west += vvp->xmpp * (x - (vvp->width/2));
    vvp->center.north_south += vvp->ympp * ((vvp->height/2) - y);
    viewport_utm_zone_check ( vvp );
  } else {
    VikCoord tmp;
    vik_viewport_screen_to_coord ( vvp, x, y, &tmp );
    vik_viewport_set_center_coord ( vvp, &tmp );
  }
}

gint vik_viewport_get_width( VikViewport *vvp )
{
  g_return_val_if_fail ( vvp != NULL, 0 );
  return vvp->width;
}

gint vik_viewport_get_height( VikViewport *vvp )
{
  g_return_val_if_fail ( vvp != NULL, 0 );
  return vvp->height;
}

void vik_viewport_screen_to_coord ( VikViewport *vvp, int x, int y, VikCoord *coord )
{
  if ( vvp->coord_mode == VIK_COORD_UTM ) {
    int zone_delta;
    struct UTM *utm = (struct UTM *) coord;
    coord->mode = VIK_COORD_UTM;

    g_return_if_fail ( vvp != NULL );

    utm->zone = vvp->center.utm_zone;
    utm->letter = vvp->center.utm_letter;
    utm->easting = ( ( x - ( vvp->width / 2) ) * vvp->xmpp ) + vvp->center.east_west;
    zone_delta = floor( (utm->easting - EASTING_OFFSET ) / vvp->utm_zone_width + 0.5 );
    utm->zone += zone_delta;
    utm->easting -= zone_delta * vvp->utm_zone_width;
    utm->northing = ( ( ( vvp->height / 2) - y ) * vvp->ympp ) + vvp->center.north_south;
  } else if ( vvp->coord_mode == VIK_COORD_LATLON ) {
    coord->mode = VIK_COORD_LATLON;
    if ( vvp->drawmode == VIK_VIEWPORT_DRAWMODE_EXPEDIA )
      calcxy_rev(&(coord->east_west), &(coord->north_south), x, y, vvp->center.east_west, vvp->center.north_south, vvp->xmpp * ALTI_TO_MPP, vvp->ympp * ALTI_TO_MPP, vvp->width/2, vvp->height/2);
    else if ( vvp->drawmode == VIK_VIEWPORT_DRAWMODE_GOOGLE ) {
      /* google */
      coord->east_west = (x - (vvp->width/2)) * vvp->google_calcx_rev_fact + vvp->center.east_west;
      coord->north_south = ((vvp->height/2) - y) * vvp->google_calcy_rev_fact + vvp->center.north_south;
    } else if ( vvp->drawmode == VIK_VIEWPORT_DRAWMODE_KH ) {
      coord->east_west = vvp->center.east_west + (180.0 * vvp->xmpp / 65536 / 256 * (x - vvp->width/2));
      coord->north_south = vvp->center.north_south + (180.0 * vvp->ympp / 65536 / 256 * (vvp->height/2 - y));
    } else if ( vvp->drawmode == VIK_VIEWPORT_DRAWMODE_MERCATOR ) {
      /* FIXMERCATOR */
      coord->east_west = vvp->center.east_west + (180.0 * vvp->xmpp / 65536 / 256 * (x - vvp->width/2));
      coord->north_south = DEMERCLAT ( MERCLAT(vvp->center.north_south) + (180.0 * vvp->ympp / 65536 / 256 * (vvp->height/2 - y)) );

#if 0
-->	THIS IS JUNK HERE.
      *y = vvp->height/2 + (65536.0 / 180 / vvp->ympp * (MERCLAT(center->lat) - MERCLAT(ll->lat)))*256.0;

      (*y - vvp->height/2) / 256 / 65536 * 180 * vvp->ympp = (MERCLAT(center->lat) - MERCLAT(ll->lat);
      DML((180.0 * vvp->ympp / 65536 / 256 * (vvp->height/2 - y)) + ML(cl)) = ll
#endif
    }
  }
}

void vik_viewport_coord_to_screen ( VikViewport *vvp, const VikCoord *coord, int *x, int *y )
{
  static VikCoord tmp;
  g_return_if_fail ( vvp != NULL );

  if ( coord->mode != vvp->coord_mode )
  {
    g_warning ( "Have to convert in vik_viewport_coord_to_screen! This should never happen!");
    vik_coord_copy_convert ( coord, vvp->coord_mode, &tmp );
    coord = &tmp;
  }

  if ( vvp->coord_mode == VIK_COORD_UTM ) {
    struct UTM *center = (struct UTM *) &(vvp->center);
    struct UTM *utm = (struct UTM *) coord;
    if ( center->zone != utm->zone && vvp->one_utm_zone )
    {
      *x = *y = VIK_VIEWPORT_UTM_WRONG_ZONE;
      return;
    }

    *x = ( (utm->easting - center->easting) / vvp->xmpp ) + (vvp->width / 2) -
  	  (center->zone - utm->zone ) * vvp->utm_zone_width / vvp->xmpp;
    *y = (vvp->height / 2) - ( (utm->northing - center->northing) / vvp->ympp );
  } else if ( vvp->coord_mode == VIK_COORD_LATLON ) {
    struct LatLon *center = (struct LatLon *) &(vvp->center);
    struct LatLon *ll = (struct LatLon *) coord;
    double xx,yy;
    if ( vvp->drawmode == VIK_VIEWPORT_DRAWMODE_EXPEDIA ) {
      calcxy ( &xx, &yy, center->lon, center->lat, ll->lon, ll->lat, vvp->xmpp * ALTI_TO_MPP, vvp->ympp * ALTI_TO_MPP, vvp->width / 2, vvp->height / 2 );
      *x = xx; *y = yy;
    } else if ( vvp->drawmode == VIK_VIEWPORT_DRAWMODE_GOOGLE ) {
      /* google */
      *x = vvp->google_calcx_fact * (ll->lon - center->lon) + (vvp->width/2);
      *y = vvp->google_calcy_fact * (center->lat - ll->lat) + (vvp->height/2);
    } else if ( vvp->drawmode == VIK_VIEWPORT_DRAWMODE_KH ) {
      /* subtract, convert to KH coords; blow it up by 256 */
      *x = vvp->width/2 + (65536.0 / 180 / vvp->xmpp * (ll->lon - center->lon))*256.0;
      *y = vvp->height/2 + (65536.0 / 180 / vvp->ympp * (center->lat - ll->lat))*256.0;
    } else if ( vvp->drawmode == VIK_VIEWPORT_DRAWMODE_MERCATOR ) {
      /* FIXMERCATOR: Optimize */
      *x = vvp->width/2 + (65536.0 / 180 / vvp->xmpp * (ll->lon - center->lon))*256.0;
      *y = vvp->height/2 + (65536.0 / 180 / vvp->ympp * (MERCLAT(center->lat) - MERCLAT(ll->lat)))*256.0;
    }
  }
}

void vik_viewport_draw_line ( VikViewport *vvp, GdkGC *gc, gint x1, gint y1, gint x2, gint y2 )
{
  if ( ! ( ( x1 < 0 && x2 < 0 ) || ( y1 < 0 && y2 < 0 ) ||
       ( x1 > vvp->width && x2 > vvp->width ) || ( y1 > vvp->height && y2 > vvp->height ) ) )
   gdk_draw_line ( vvp->scr_buffer, gc, x1, y1, x2, y2);
}

void vik_viewport_draw_rectangle ( VikViewport *vvp, GdkGC *gc, gboolean filled, gint x1, gint y1, gint x2, gint y2 )
{
  if ( x1 > -10 && x1 < vvp->width + 10 && y1 > -10 && y1 < vvp->height + 10 )
    gdk_draw_rectangle ( vvp->scr_buffer, gc, filled, x1, y1, x2, y2);
}

void vik_viewport_draw_string ( VikViewport *vvp, GdkFont *font, GdkGC *gc, gint x1, gint y1, const gchar *string )
{
  if ( x1 > -100 && x1 < vvp->width + 100 && y1 > -100 && y1 < vvp->height + 100 )
    gdk_draw_string ( vvp->scr_buffer, font, gc, x1, y1, string );
}

/* shouldn't use this -- slow -- change the alpha channel instead. */
void vik_viewport_draw_pixbuf_with_alpha ( VikViewport *vvp, GdkPixbuf *pixbuf, gint alpha,
                                           gint src_x, gint src_y, gint dest_x, gint dest_y, gint w, gint h )
{
  gint real_dest_x = MAX(dest_x,0);
  gint real_dest_y = MAX(dest_y,0);

  if ( alpha == 0 )
    return; /* don't waste your time */

  if ( w > vvp->alpha_pixbuf_width || h > vvp->alpha_pixbuf_height )
  {
    if ( vvp->alpha_pixbuf )
      g_object_unref ( G_OBJECT ( vvp->alpha_pixbuf ) );
    vvp->alpha_pixbuf_width = MAX(w,vvp->alpha_pixbuf_width);
    vvp->alpha_pixbuf_height = MAX(h,vvp->alpha_pixbuf_height);
    vvp->alpha_pixbuf = gdk_pixbuf_new ( GDK_COLORSPACE_RGB, FALSE, 8, vvp->alpha_pixbuf_width, vvp->alpha_pixbuf_height );
  }

  w = MIN(w,vvp->width - dest_x);
  h = MIN(h,vvp->height - dest_y);

  /* check that we are drawing within boundaries. */
  src_x += (real_dest_x - dest_x);
  src_y += (real_dest_y - dest_y);
  w -= (real_dest_x - dest_x);
  h -= (real_dest_y - dest_y);

  gdk_pixbuf_get_from_drawable ( vvp->alpha_pixbuf, vvp->scr_buffer, NULL,
                                 real_dest_x, real_dest_y, 0, 0, w, h );

  /* do a composite */
  gdk_pixbuf_composite ( pixbuf, vvp->alpha_pixbuf, 0, 0, w, h, -src_x, -src_y, 1, 1, 0, alpha );

  /* draw pixbuf_tmp */
  vik_viewport_draw_pixbuf ( vvp, vvp->alpha_pixbuf, 0, 0, real_dest_x, real_dest_y, w, h );
}

void vik_viewport_draw_pixbuf ( VikViewport *vvp, GdkPixbuf *pixbuf, gint src_x, gint src_y,
                              gint dest_x, gint dest_y, gint w, gint h )
{
  gdk_draw_pixbuf ( vvp->scr_buffer,
// GTK_WIDGET(vvp)->style->black_gc,
NULL,
 pixbuf,
                    src_x, src_y, dest_x, dest_y, w, h,
                    GDK_RGB_DITHER_NONE, 0, 0 );
}

void vik_viewport_draw_arc ( VikViewport *vvp, GdkGC *gc, gboolean filled, gint x, gint y, gint width, gint height, gint angle1, gint angle2 )
{
  gdk_draw_arc ( vvp->scr_buffer, gc, filled, x, y, width, height, angle1, angle2 );
}


void vik_viewport_draw_polygon ( VikViewport *vvp, GdkGC *gc, gboolean filled, GdkPoint *points, gint npoints )
{
  gdk_draw_polygon ( vvp->scr_buffer, gc, filled, points, npoints );
}

VikCoordMode vik_viewport_get_coord_mode ( const VikViewport *vvp )
{
  g_assert ( vvp );
  return vvp->coord_mode;
}

static void viewport_set_coord_mode ( VikViewport *vvp, VikCoordMode mode )
{
  g_return_if_fail ( vvp != NULL );
  vvp->coord_mode = mode;
  vik_coord_convert ( &(vvp->center), mode );
}

/* Thanks GPSDrive */
static gboolean calcxy_rev(double *lg, double *lt, gint x, gint y, double zero_long, double zero_lat, double pixelfact_x, double pixelfact_y, gint mapSizeX2, gint mapSizeY2 )
{
  int px, py;
  gdouble dif, lat, lon;
  double Ra = Radius[90+(gint)zero_lat];

  px = (mapSizeX2 - x) * pixelfact_x;
  py = (-mapSizeY2 + y) * pixelfact_y;

  lat = zero_lat - py / Ra;
  lat = zero_lat - py / Ra;
  lon =
    zero_long -
    px / (Ra *
         cos (lat * DEG2RAD));

  dif = lat * (1 - (cos ((fabs (lon - zero_long)) * DEG2RAD)));
  lat = lat - dif / 1.5;
  lon =
    zero_long -
    px / (Ra *
              cos (lat * DEG2RAD));

  *lt = lat;
  *lg = lon;
  return (TRUE);
}

/* Thanks GPSDrive */
static gboolean calcxy(double *x, double *y, double lg, double lt, double zero_long, double zero_lat, double pixelfact_x, double pixelfact_y, gint mapSizeX2, gint mapSizeY2 )
{
    double dif;
    double Ra;
    gint mapSizeX = 2 * mapSizeX2;
    gint mapSizeY = 2 * mapSizeY2;

    g_assert ( lt >= -90.0 && lt <= 90.0 );
//    lg *= rad2deg; // FIXME, optimize equations
//    lt *= rad2deg;
    Ra = Radius[90+(gint)lt];
    *x = Ra *
         cos (lt*DEG2RAD) * (lg - zero_long);
    *y = Ra * (lt - zero_lat);
    dif = Ra * RAD2DEG * (1 - (cos ((DEG2RAD * (lg - zero_long)))));
    *y = *y + dif / 1.85;
    *x = *x / pixelfact_x;
    *y = *y / pixelfact_y;
    *x = mapSizeX2 - *x;
    *y += mapSizeY2;
    if ((*x < 0)||(*x >= mapSizeX)||(*y < 0)||(*y >= mapSizeY))
        return (FALSE);
    return (TRUE);
}

static void viewport_init_ra()
{
  static gboolean done_before = FALSE;
  if ( !done_before )
  {
    gint i;
    for ( i = -90; i <= 90; i++)
      Radius[i+90] = calcR ( (double)i ) * DEG2RAD;
    done_before = TRUE;
  }
}

double calcR (double lat)
{
    double a = 6378.137, r, sc, x, y, z;
    double e2 = 0.081082 * 0.081082;
    /*
     * the radius of curvature of an ellipsoidal Earth in the plane of the
     * meridian is given by
     *
     * R' = a * (1 - e^2) / (1 - e^2 * (sin(lat))^2)^(3/2)
     *
     *
     * where a is the equatorial radius, b is the polar radius, and e is
     * the eccentricity of the ellipsoid = sqrt(1 - b^2/a^2)
     *
     * a = 6378 km (3963 mi) Equatorial radius (surface to center distance)
     * b = 6356.752 km (3950 mi) Polar radius (surface to center distance) e
     * = 0.081082 Eccentricity
     */

    lat = lat * DEG2RAD;
    sc = sin (lat);
    x = a * (1.0 - e2);
    z = 1.0 - e2 * sc * sc;
    y = pow (z, 1.5);
    r = x / y;
    r = r * 1000.0;
    return r;
}

gboolean vik_viewport_is_one_zone ( VikViewport *vvp )
{
  return vvp->coord_mode == VIK_COORD_UTM && vvp->one_utm_zone;
}

void vik_viewport_draw_layout ( VikViewport *vvp, GdkGC *gc, gint x, gint y, PangoLayout *layout )
{
  if ( x > -100 && x < vvp->width + 100 && y > -100 && y < vvp->height + 100 )
    gdk_draw_layout ( vvp->scr_buffer, gc, x, y, layout );
}

void vik_gc_get_fg_color ( GdkGC *gc, GdkColor *dest )
{
  static GdkGCValues values;
  gdk_gc_get_values ( gc, &values );
  gdk_colormap_query_color ( gdk_colormap_get_system(), values.foreground.pixel, dest );
}

GdkFunction vik_gc_get_function ( GdkGC *gc )
{
  static GdkGCValues values;
  gdk_gc_get_values ( gc, &values );
  return values.function;
}

void vik_viewport_set_drawmode ( VikViewport *vvp, VikViewportDrawMode drawmode )
{
  vvp->drawmode = drawmode;
  if ( drawmode == VIK_VIEWPORT_DRAWMODE_UTM )
    viewport_set_coord_mode ( vvp, VIK_COORD_UTM );
  else {
    viewport_set_coord_mode ( vvp, VIK_COORD_LATLON );
    if ( drawmode == VIK_VIEWPORT_DRAWMODE_GOOGLE )
      viewport_google_rezoom ( vvp );
  }
}

VikViewportDrawMode vik_viewport_get_drawmode ( VikViewport *vvp )
{
  return vvp->drawmode;
}

static void viewport_google_rezoom ( VikViewport *vvp )
{
  vvp->google_calcx_fact = (GOOGLEMAPS_ZOOM_ONE_MPP * 65536.0 * 0.7716245833877 / vvp->xmpp);
  vvp->google_calcy_fact = (GOOGLEMAPS_ZOOM_ONE_MPP * 65536.0 / vvp->ympp);
  vvp->google_calcx_rev_fact = 1 / vvp->google_calcx_fact;
  vvp->google_calcy_rev_fact = 1 / vvp->google_calcy_fact;
}
