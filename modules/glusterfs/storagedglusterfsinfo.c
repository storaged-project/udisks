/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2015 Samikshan Bairagya <sbairagy@redhat.com>
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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <libxml/parser.h>
#include <glib.h>
#include <stdlib.h>
#include <string.h>

#include <src/storagedlogging.h>

#include "storagedglusterfsinfo.h"

GVariantBuilder *builder, *b;

static void
add_volume_name_to_list(xmlNode *cur)
{
  cur = cur->children;
  while (cur != NULL) {
    if (cur->type == XML_ELEMENT_NODE) {
      if (xmlStrEqual(cur->name, (const xmlChar *) "name") == 1) {
        g_variant_builder_add (builder, "s", xmlNodeGetContent(cur));
        return;
      }
    }
    cur = cur->next;
  }
}

static void
get_glusterfs_volume_names(xmlNode * a_node)
{
  xmlNode *cur_node = NULL;

  for (cur_node = a_node; cur_node; cur_node = cur_node->next) {
    if (cur_node->type == XML_ELEMENT_NODE) {
      if (strcmp (cur_node->name, "volume") == 0)
        add_volume_name_to_list (cur_node);
    }

    get_glusterfs_volume_names (cur_node->children);
  }
}

static xmlChar *
get_val_for_key (xmlNode *cur, const xmlChar *key)
{
  cur = cur->children;
  while (cur != NULL) {
    if (cur->type == XML_ELEMENT_NODE) {
      if (xmlStrEqual(cur->name, key) == 1)
        return xmlNodeGetContent (cur);
    }
    cur = cur->next;
  }
  return NULL;
}

static GVariant *
get_brick_info (xmlNode *cur)
{
  GVariantBuilder *brick_info_builder;
  brick_info_builder = g_variant_builder_new (G_VARIANT_TYPE("a{sv}"));

  cur = cur->children;
  while (cur != NULL) {
    if (cur->type == XML_ELEMENT_NODE) {
      storaged_debug ("%s: %s", cur->name, xmlNodeGetContent (cur));
      g_variant_builder_add (brick_info_builder, "{sv}", cur->name, g_variant_new_string (xmlNodeGetContent (cur)));
    }
    cur = cur->next;
  }
  return g_variant_builder_end (brick_info_builder);
}

static GVariant *
get_brick_list (xmlNode *cur)
{
  GVariantBuilder *brick_list_builder;
  brick_list_builder = g_variant_builder_new (G_VARIANT_TYPE ("av"));

  cur = cur->children;
  while (cur != NULL) {
    if (cur->type == XML_ELEMENT_NODE) {
      if (xmlStrEqual (cur->name, "brick"))
        g_variant_builder_add (brick_list_builder, "v", get_brick_info (cur));
    }
    cur = cur->next;
  }
  return g_variant_builder_end (brick_list_builder);
}

static GVariant *
get_bricks (xmlNode *cur)
{
  cur = cur->children;
  while (cur != NULL) {
    if (cur->type == XML_ELEMENT_NODE) {
      if (xmlStrEqual (cur->name, "bricks"))
        return get_brick_list (cur);
    }
    cur = cur->next;
  }
  return NULL;
}

static void
get_glusterfs_volume_info (xmlNode * a_node)
{
  xmlNode *cur_node = NULL;
  gchar *name, *id;
  int status, brickcount;

  for (cur_node = a_node; cur_node; cur_node = cur_node->next) {
    if (cur_node->type == XML_ELEMENT_NODE) {
      if (strcmp (cur_node->name, "volume") == 0) {
        name = get_val_for_key (cur_node, "name");
        id = get_val_for_key (cur_node, "id");
        status = atoi (get_val_for_key (cur_node, "status"));
        brickcount = atoi (get_val_for_key (cur_node, "brickCount"));

        g_variant_builder_add (b, "{sv}", "id", g_variant_new_string (name));
        g_variant_builder_add (b, "{sv}", "name", g_variant_new_string (id));
        g_variant_builder_add (b, "{sv}", "status", g_variant_new_uint32 (status));
        g_variant_builder_add (b, "{sv}", "brickCount", g_variant_new_uint32 (brickcount));
        g_variant_builder_add (b, "{sv}", "bricks", get_bricks (cur_node));

        return;
      }
    }

    get_glusterfs_volume_info (cur_node->children);
  }
}

GVariant *
storaged_process_glusterfs_volume_info (const gchar *xml_info)
{
  xmlDoc  *doc = NULL;
  xmlNode *cur = NULL;

  LIBXML_TEST_VERSION
  /*parse the xml string and get the DOM */
  doc = xmlParseDoc(xml_info);

  if (doc == NULL)
    {
      storaged_error ("error: could not parse XML doc: \n %s", xml_info);
      return NULL;
    }

  /*Get the root element node */
  cur = xmlDocGetRootElement(doc);
  if (!cur)
    {
      storaged_error ("Could not get root element");
    }

  b = g_variant_builder_new (G_VARIANT_TYPE("a{sv}"));

  get_glusterfs_volume_info (cur);

  /*free the document */
  xmlFreeDoc(doc);

  /*
   * Free the global variables that may
   * have been allocated by the parser.
   */
  xmlCleanupParser();

  return g_variant_builder_end (b);
}

GVariant *
storaged_process_glusterfs_volume_info_all (const gchar *xml_info)
{
  xmlDoc  *doc = NULL;
  xmlNode *cur = NULL;

  LIBXML_TEST_VERSION
  /*parse the xml string and get the DOM */
  doc = xmlParseDoc(xml_info);

  if (doc == NULL)
    {
      storaged_error ("error: could not parse XML doc: \n %s", xml_info);
      return NULL;
    }

  /*Get the root element node */
  cur = xmlDocGetRootElement(doc);
  if (!cur)
    {
      storaged_error ("Could not get root element");
    }
  builder = g_variant_builder_new (G_VARIANT_TYPE("as"));
  g_variant_builder_init (builder, G_VARIANT_TYPE("as"));

  /* get_glusterfs_volume_names (cur); */
  get_glusterfs_volume_names (cur);

  /*free the document */
  xmlFreeDoc(doc);

  /*
   * Free the global variables that may
   * have been allocated by the parser.
   */
  xmlCleanupParser();

  return g_variant_builder_end (builder);
}

