/*
 * viking -- GPS Data and Topo Analyzer, Explorer, and Manager
 *
 * Copyright (C) 2012-2013, Guilhem Bonnefille <guilhem.bonnefille@gmail.com>
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
 */

/**
 * SECTION:vikroutingengine
 * @short_description: the base class to describe routing engine
 * 
 * The #VikRoutingEngine class is both the interface and the base class
 * for the hierarchie of routing engines.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <glib.h>
#include <glib/gstdio.h>
#include <glib/gi18n.h>

#include "curl_download.h"
#include "babel.h"

#include "vikroutingengine.h"

static void vik_routing_engine_finalize ( GObject *gob );
static DownloadMapOptions *vik_routing_engine_get_download_options_default ( VikRoutingEngine *self );
static GObjectClass *parent_class;

typedef struct _VikRoutingPrivate VikRoutingPrivate;
struct _VikRoutingPrivate
{
	gchar *id;
	gchar *label;
	gchar *format;
};

#define VIK_ROUTING_ENGINE_PRIVATE(o)  (G_TYPE_INSTANCE_GET_PRIVATE ((o), VIK_ROUTING_ENGINE_TYPE, VikRoutingPrivate))

/* properties */
enum
{
  PROP_0,

  PROP_ID,
  PROP_LABEL,
  PROP_FORMAT,
};

G_DEFINE_ABSTRACT_TYPE (VikRoutingEngine, vik_routing_engine, G_TYPE_OBJECT)

static void
vik_routing_engine_set_property (GObject      *object,
                          guint         property_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
  VikRoutingPrivate *priv = VIK_ROUTING_ENGINE_PRIVATE ( object );

  switch (property_id)
    {
    case PROP_ID:
      g_free (priv->id);
      priv->id = g_strdup(g_value_get_string (value));
      break;

    case PROP_LABEL:
      g_free (priv->label);
      priv->label = g_strdup(g_value_get_string (value));
      break;

    case PROP_FORMAT:
      g_free (priv->format);
      priv->format = g_strdup(g_value_get_string (value));
      break;

    default:
      /* We don't have any other property... */
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
vik_routing_engine_get_property (GObject    *object,
                          guint       property_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
  VikRoutingPrivate *priv = VIK_ROUTING_ENGINE_PRIVATE ( object );

  switch (property_id)
    {
    case PROP_ID:
      g_value_set_string (value, priv->id);
      break;

    case PROP_LABEL:
      g_value_set_string (value, priv->label);
      break;

    case PROP_FORMAT:
      g_value_set_string (value, priv->format);
      break;

    default:
      /* We don't have any other property... */
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
vik_routing_engine_class_init ( VikRoutingEngineClass *klass )
{
  GObjectClass *object_class;
  VikRoutingEngineClass *routing_class;
  GParamSpec *pspec = NULL;

  object_class = G_OBJECT_CLASS (klass);

  object_class->set_property = vik_routing_engine_set_property;
  object_class->get_property = vik_routing_engine_get_property;
  object_class->finalize = vik_routing_engine_finalize;

  parent_class = g_type_class_peek_parent (klass);

  routing_class = VIK_ROUTING_ENGINE_CLASS ( klass );
  routing_class->find = NULL;
  routing_class->get_download_options = vik_routing_engine_get_download_options_default;
  routing_class->get_url_for_coords = NULL;


  pspec = g_param_spec_string ("id",
                               "Identifier",
                               "The identifier of the routing engine",
                               "<no-set>" /* default value */,
                               G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE);
  g_object_class_install_property (object_class, PROP_ID, pspec);
  
  pspec = g_param_spec_string ("label",
                               "Label",
                               "The label of the routing engine",
                               "<no-set>" /* default value */,
                               G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE);
  g_object_class_install_property (object_class, PROP_LABEL, pspec);
    
  pspec = g_param_spec_string ("format",
                               "Format",
                               "The format of the output (see gpsbabel)",
                               "<no-set>" /* default value */,
                               G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE);
  g_object_class_install_property (object_class, PROP_FORMAT, pspec);

  g_type_class_add_private (klass, sizeof (VikRoutingPrivate));
}

static void
vik_routing_engine_init ( VikRoutingEngine *self )
{
  VikRoutingPrivate *priv = VIK_ROUTING_ENGINE_PRIVATE (self);

  priv->id = NULL;
  priv->label = NULL;
  priv->format = NULL;
}

static void
vik_routing_engine_finalize ( GObject *self )
{
  VikRoutingPrivate *priv = VIK_ROUTING_ENGINE_PRIVATE (self);

  g_free (priv->id);
  priv->id = NULL;

  g_free (priv->label);
  priv->label = NULL;

  g_free (priv->format);
  priv->format = NULL;

  G_OBJECT_CLASS(parent_class)->finalize(self);
}

static DownloadMapOptions *
vik_routing_engine_get_download_options_default ( VikRoutingEngine *self )
{
	// Default: return NULL
	return NULL;
}

static gchar *
vik_routing_engine_get_url_for_coords ( VikRoutingEngine *self, struct LatLon start, struct LatLon end )
{
	VikRoutingEngineClass *klass;
	
	g_return_val_if_fail ( VIK_IS_ROUTING_ENGINE (self), NULL );
	klass = VIK_ROUTING_ENGINE_GET_CLASS( self );
	g_return_val_if_fail ( klass->get_url_for_coords != NULL, NULL );
	
	return klass->get_url_for_coords( self, start, end );
}

static DownloadMapOptions *
vik_routing_engine_get_download_options ( VikRoutingEngine *self )
{
	VikRoutingEngineClass *klass;
	
	g_return_val_if_fail ( VIK_IS_ROUTING_ENGINE (self), NULL );
	klass = VIK_ROUTING_ENGINE_GET_CLASS( self );
	g_return_val_if_fail ( klass->get_download_options != NULL, NULL );

	return klass->get_download_options( self );
}

int
vik_routing_engine_find ( VikRoutingEngine *self, VikTrwLayer *vtl, struct LatLon start, struct LatLon end )
{
  gchar *uri;
  int ret = 0;  /* OK */

  uri = vik_routing_engine_get_url_for_coords(self, start, end);

  DownloadMapOptions *options = vik_routing_engine_get_download_options(self);
  
  gchar *format = vik_routing_engine_get_format ( self );
  a_babel_convert_from_url ( vtl, uri, format, NULL, NULL, options );

  g_free(uri);
  return ret;
}

/**
 * vik_routing_engine_get_id:
 * 
 * Returns: the id of self
 */
gchar *
vik_routing_engine_get_id ( VikRoutingEngine *self )
{
  VikRoutingPrivate *priv = VIK_ROUTING_ENGINE_PRIVATE (self);

  return priv->id;
}

/**
 * vik_routing_engine_get_label:
 * 
 * Returns: the label of self
 */
gchar *
vik_routing_engine_get_label ( VikRoutingEngine *self )
{
  VikRoutingPrivate *priv = VIK_ROUTING_ENGINE_PRIVATE (self);

  return priv->label;
}

/**
 * vik_routing_engine_get_format:
 * 
 * Returns: the format of self
 */
gchar *
vik_routing_engine_get_format ( VikRoutingEngine *self )
{
  VikRoutingPrivate *priv = VIK_ROUTING_ENGINE_PRIVATE (self);

  return priv->format;
}