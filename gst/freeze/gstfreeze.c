/* gst-freeze -- Source freezer
 * Copyright (C) 2005 Gergely Nagy <gergely.nagy@neteyes.hu>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * version 2.1, as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307  USA
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>

#include "gstfreeze.h"

GST_DEBUG_CATEGORY (freeze_debug);
#define GST_CAT_DEFAULT freeze_debug

static GstElementDetails freeze_details = GST_ELEMENT_DETAILS ("Stream freezer",
    "Generic",
    "Makes a stream from buffers of data",
    "Gergely Nagy <gergely.nagy@neteyes.hu>");

static GstStaticPadTemplate gst_freeze_src_template =
GST_STATIC_PAD_TEMPLATE ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate gst_freeze_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

enum
{
  ARG_0,
  ARG_MAX_BUFFERS,
};

#define parent_class \
  GST_ELEMENT_CLASS (g_type_class_peek_parent \
		     (g_type_class_peek (GST_TYPE_FREEZE)))

static GstElementStateReturn
gst_freeze_change_state (GstElement * element)
{
  GstFreeze *freeze = GST_FREEZE (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_READY_TO_PAUSED:
    case GST_STATE_PLAYING_TO_PAUSED:
    case GST_STATE_READY_TO_NULL:
      break;
    case GST_STATE_PAUSED_TO_READY:
    case GST_STATE_NULL_TO_READY:
    case GST_STATE_PAUSED_TO_PLAYING:
      freeze->timestamp_offset = freeze->running_time = 0;
      break;
  }

  return parent_class->change_state (element);
}

static void
gst_freeze_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstFreeze *freeze = GST_FREEZE (object);

  switch (prop_id) {
    case ARG_MAX_BUFFERS:
      freeze->max_buffers = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_freeze_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstFreeze *freeze = GST_FREEZE (object);

  switch (prop_id) {
    case ARG_MAX_BUFFERS:
      g_value_set_uint (value, freeze->max_buffers);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_freeze_dispose (GObject * object)
{
  guint i;
  GstFreeze *freeze = GST_FREEZE (object);

  if (freeze->buffers != NULL) {
    for (i = 0; i < g_list_length (freeze->buffers); i++)
      gst_buffer_unref (GST_BUFFER (g_list_nth_data (freeze->buffers, i)));

    g_list_free (freeze->buffers);
    freeze->buffers = NULL;
    freeze->current = NULL;
  }

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_freeze_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_set_details (element_class, &freeze_details);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_freeze_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_freeze_src_template));
}

static void
gst_freeze_class_init (GstFreezeClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  element_class->change_state = gst_freeze_change_state;

  g_object_class_install_property
      (object_class, ARG_MAX_BUFFERS,
      g_param_spec_uint ("max-buffers", "max-buffers",
          "Maximum number of buffers", 0, G_MAXUINT, 1, G_PARAM_READWRITE));

  object_class->set_property = gst_freeze_set_property;
  object_class->get_property = gst_freeze_get_property;
  object_class->dispose = gst_freeze_dispose;
}

static void
gst_freeze_loop (GstElement * element)
{
  GstFreeze *freeze;

  freeze = GST_FREEZE (element);

  if (!GST_PAD_IS_USABLE (freeze->sinkpad)) {
    if (freeze->buffers == NULL) {
      GST_DEBUG_OBJECT (freeze, "sink pad is not (yet?) usable");
      return;
    }
  } else {
    GstData *data;

    data = gst_pad_pull (GST_PAD (freeze->sinkpad));
    if (GST_IS_EVENT (data)) {
      GstEvent *event = GST_EVENT (data);

      switch (GST_EVENT_TYPE (event)) {
        case GST_EVENT_EOS:
          GST_DEBUG_OBJECT (freeze, "EOS on sink pad %s",
              gst_pad_get_name (GST_PAD (freeze->sinkpad)));
          gst_event_unref (event);
          break;
        default:
          gst_pad_event_default (GST_PAD (freeze->sinkpad), GST_EVENT (data));
          break;
      }
    } else if (GST_IS_BUFFER (data)) {
      if (g_list_length (freeze->buffers) < freeze->max_buffers ||
          freeze->max_buffers == 0) {
        freeze->buffers = g_list_append (freeze->buffers, GST_BUFFER (data));
        GST_DEBUG_OBJECT (freeze, "accepted buffer %u",
            g_list_length (freeze->buffers) - 1);
      } else {
        gst_buffer_unref (GST_BUFFER (data));
      }
    }
    data = NULL;
  }

  if (freeze->buffers == NULL) {
    GST_DEBUG_OBJECT (freeze, "no buffers yet");
    return;
  }

  if (freeze->current != NULL) {
    GST_DEBUG_OBJECT (freeze, "switching to next buffer");
    freeze->current = freeze->current->next;
  }

  if (freeze->current == NULL) {
    if (freeze->max_buffers > 1)
      GST_DEBUG_OBJECT (freeze, "restarting the loop");
    freeze->current = freeze->buffers;
  }

  GST_BUFFER_TIMESTAMP (freeze->current->data) = freeze->timestamp_offset +
      freeze->running_time;
  freeze->running_time += GST_BUFFER_DURATION (freeze->current->data);

  gst_buffer_ref (GST_BUFFER (freeze->current->data));
  gst_pad_push (freeze->srcpad, GST_DATA (freeze->current->data));
}

static void
gst_freeze_init (GstFreeze * freeze)
{
  GST_FLAG_SET (freeze, GST_ELEMENT_EVENT_AWARE);

  freeze->sinkpad = gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_freeze_sink_template), "sink");
  gst_element_add_pad (GST_ELEMENT (freeze), freeze->sinkpad);
  gst_element_set_loop_function (GST_ELEMENT (freeze), gst_freeze_loop);
  gst_pad_set_getcaps_function (freeze->sinkpad, gst_pad_proxy_getcaps);
  gst_pad_set_link_function (freeze->sinkpad, gst_pad_proxy_pad_link);

  freeze->srcpad = gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_freeze_src_template), "src");
  gst_element_add_pad (GST_ELEMENT (freeze), freeze->srcpad);
  gst_pad_set_getcaps_function (freeze->srcpad, gst_pad_proxy_getcaps);
  gst_pad_set_link_function (freeze->srcpad, gst_pad_proxy_pad_link);

  freeze->timestamp_offset = 0;
  freeze->running_time = 0;
  freeze->buffers = NULL;
  freeze->current = NULL;
  freeze->max_buffers = 1;
}

GType
gst_freeze_get_type (void)
{
  static GType freeze_type = 0;

  if (!freeze_type) {
    static const GTypeInfo freeze_info = {
      sizeof (GstFreezeClass),
      gst_freeze_base_init,
      NULL,
      (GClassInitFunc) gst_freeze_class_init,
      NULL,
      NULL,
      sizeof (GstFreeze),
      0,
      (GInstanceInitFunc) gst_freeze_init,
    };

    freeze_type = g_type_register_static
        (GST_TYPE_ELEMENT, "GstFreeze", &freeze_info, 0);
  }

  return freeze_type;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (freeze_debug, "freeze", 0, "Stream freezer");

  return gst_element_register (plugin, "freeze", GST_RANK_NONE,
      GST_TYPE_FREEZE);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "freeze",
    "Stream freezer",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE, GST_ORIGIN);

/*
 * Local variables:
 * mode: c
 * file-style: k&r
 * c-basic-offset: 2
 * arch-tag: fb0ee62b-cf74-46c0-8e62-93b58bacc0ed
 * End:
 */
