/* GStreamer
 * Copyright (C) <2004> David A. Schleef <ds@schleef.org>
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/video/video.h>

#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include <librfb/rfb.h>

GST_DEBUG_CATEGORY (gst_debug_rfbsrc);
#define GST_CAT_DEFAULT gst_debug_rfbsrc

#define GST_TYPE_RFBSRC \
  (gst_rfbsrc_get_type())
#define GST_RFBSRC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_RFBSRC,GstRfbsrc))
#define GST_RFBSRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_RFBSRC,GstRfbsrc))
#define GST_IS_RFBSRC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_RFBSRC))
#define GST_IS_RFBSRC_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_RFBSRC))

typedef struct _GstRfbsrc GstRfbsrc;
typedef struct _GstRfbsrcClass GstRfbsrcClass;

struct _GstRfbsrc
{
  GstElement element;

  GstPad *srcpad;

  RfbDecoder *decoder;

  char *server;
  int port;

  guint8 *frame;
  gboolean go;
  gboolean inter;

  unsigned int button_mask;

  double framerate;

  GstClock *clock;
  GstClockTime timestamp;
};

struct _GstRfbsrcClass
{
  GstElementClass parent_class;
};

GType
gst_rfbsrc_get_type (void)
    G_GNUC_CONST;



     static GstElementDetails rfbsrc_details =
         GST_ELEMENT_DETAILS ("RFB (VNC) source",
    "Source/Video",
    "Connects to a VNC server and decodes RFB stream",
    "David A. Schleef <ds@schleef.org>");

/* GstRfbsrc signals and args */
     enum
     {
       /* FILL ME */
       LAST_SIGNAL
     };

     enum
     {
       ARG_0,
       ARG_SERVER,
       ARG_PORT,
       /* FILL ME */
     };

     static void gst_rfbsrc_base_init (gpointer g_class);
     static void gst_rfbsrc_class_init (GstRfbsrcClass * klass);
     static void gst_rfbsrc_init (GstRfbsrc * rfbsrc);
     static GstElementStateReturn gst_rfbsrc_change_state (GstElement *
    element);

     static void gst_rfbsrc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
     static void gst_rfbsrc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

     static GstData *gst_rfbsrc_get (GstPad * pad);

     static gboolean gst_rfbsrc_connect_to_server (GstRfbsrc * rfbsrc);
     static gboolean gst_rfbsrc_handle_input (GstRfbsrc * rfbsrc, int timeout);
     static void gst_rfbsrc_set_clock (GstElement * element, GstClock * clock);

     static const GstQueryType *gst_rfbsrc_get_query_types (GstPad * pad);
     static gboolean gst_rfbsrc_src_query (GstPad * pad,
    GstQueryType type, GstFormat * format, gint64 * value);
     static void gst_rfbsrc_paint_rect (RfbDecoder * decoder, int x, int y,
    int w, int h, guint8 * data);
     static gboolean gst_rfbsrc_handle_src_event (GstPad * pad,
    GstEvent * event);

     static GstCaps *gst_rfbsrc_getcaps (GstPad * pad);
     static GstPadLinkReturn gst_rfbsrc_link (GstPad * pad,
    const GstCaps * caps);
     static GstCaps *gst_rfbsrc_fixate (GstPad * pad, const GstCaps * caps);

     static GstElementClass *parent_class = NULL;


     static GstStaticPadTemplate gst_rfbsrc_src_template =
         GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw-rgb, "
        "bpp = (int) 32, "
        "depth = (int) 24, "
        "endianness = (int) BIG_ENDIAN, "
        "red_mask = (int) 0x0000ff00, "
        "green_mask = (int) 0x00ff0000, "
        "blue_mask = (int) 0xff000000, "
        "width = [ 16, 4096 ], "
        "height = [ 16, 4096 ], " "framerate = [ 1.0, 10.0] ")
    );

GType
gst_rfbsrc_get_type (void)
{
  static GType rfbsrc_type = 0;

  if (!rfbsrc_type) {
    static const GTypeInfo rfbsrc_info = {
      sizeof (GstRfbsrcClass),
      gst_rfbsrc_base_init,
      NULL,
      (GClassInitFunc) gst_rfbsrc_class_init,
      NULL,
      NULL,
      sizeof (GstRfbsrc),
      0,
      (GInstanceInitFunc) gst_rfbsrc_init,
    };

    rfbsrc_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstRfbsrc", &rfbsrc_info, 0);
  }
  return rfbsrc_type;
}

static void
gst_rfbsrc_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (element_class, &rfbsrc_details);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rfbsrc_src_template));
}

static void
gst_rfbsrc_class_init (GstRfbsrcClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

#if 0
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_WIDTH,
      g_param_spec_int ("width", "width", "Default width",
          1, G_MAXINT, 320, G_PARAM_READWRITE));
#endif

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  g_object_class_install_property (gobject_class, ARG_SERVER,
      g_param_spec_string ("server", "Server", "Server",
          "127.0.0.1", G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_PORT,
      g_param_spec_int ("port", "Port", "Port",
          1, 65535, 5900, G_PARAM_READWRITE));

  gobject_class->set_property = gst_rfbsrc_set_property;
  gobject_class->get_property = gst_rfbsrc_get_property;

  gstelement_class->change_state = gst_rfbsrc_change_state;
  gstelement_class->set_clock = gst_rfbsrc_set_clock;
}

static GstElementStateReturn
gst_rfbsrc_change_state (GstElement * element)
{
  GstRfbsrc *rfbsrc;
  gboolean ret;

  GST_DEBUG ("enter");

  rfbsrc = GST_RFBSRC (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      ret = gst_rfbsrc_connect_to_server (rfbsrc);
      if (!ret) {
        GST_ELEMENT_ERROR (element, STREAM, TOO_LAZY,
            ("failed to connect to server"), (NULL));
        return GST_STATE_FAILURE;
      }
      break;
    case GST_STATE_READY_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_READY:
      //rfbsrc->timestamp_offset = 0;
      //rfbsrc->n_frames = 0;
      break;
    case GST_STATE_READY_TO_NULL:
      if (rfbsrc->frame) {
        g_free (rfbsrc->frame);
        rfbsrc->frame = NULL;
      }
      break;
  }

  return parent_class->change_state (element);
}

static void
gst_rfbsrc_init (GstRfbsrc * rfbsrc)
{
  GST_DEBUG ("gst_rfbsrc_init");

  rfbsrc->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_rfbsrc_src_template), "src");
  gst_element_add_pad (GST_ELEMENT (rfbsrc), rfbsrc->srcpad);
  gst_pad_set_getcaps_function (rfbsrc->srcpad, gst_rfbsrc_getcaps);
  gst_pad_set_link_function (rfbsrc->srcpad, gst_rfbsrc_link);
  gst_pad_set_fixate_function (rfbsrc->srcpad, gst_rfbsrc_fixate);
  gst_pad_set_get_function (rfbsrc->srcpad, gst_rfbsrc_get);
  gst_pad_set_query_function (rfbsrc->srcpad, gst_rfbsrc_src_query);
  gst_pad_set_query_type_function (rfbsrc->srcpad, gst_rfbsrc_get_query_types);
  gst_pad_set_event_function (rfbsrc->srcpad, gst_rfbsrc_handle_src_event);

  rfbsrc->server = g_strdup ("127.0.0.1");
  rfbsrc->port = 5900;
}

static GstCaps *
gst_rfbsrc_getcaps (GstPad * pad)
{
  GstRfbsrc *rfbsrc;
  GstCaps *caps;

  rfbsrc = GST_RFBSRC (gst_pad_get_parent (pad));

  caps = gst_caps_copy (gst_pad_get_pad_template_caps (pad));

  if (rfbsrc->decoder && rfbsrc->decoder->inited) {
    gst_caps_set_simple (caps,
        "width", G_TYPE_INT, rfbsrc->decoder->width,
        "height", G_TYPE_INT, rfbsrc->decoder->height, NULL);
  }

  return caps;
}

static GstCaps *
gst_rfbsrc_fixate (GstPad * pad, const GstCaps * caps)
{
  GstStructure *structure;
  GstCaps *newcaps;

  if (gst_caps_get_size (caps) > 1)
    return NULL;

  newcaps = gst_caps_copy (caps);
  structure = gst_caps_get_structure (newcaps, 0);

  if (gst_caps_structure_fixate_field_nearest_double (structure, "framerate",
          10.0)) {
    return newcaps;
  }

  gst_caps_free (newcaps);
  return NULL;
}

static GstPadLinkReturn
gst_rfbsrc_link (GstPad * pad, const GstCaps * caps)
{
  GstRfbsrc *rfbsrc;
  GstStructure *structure;

  rfbsrc = GST_RFBSRC (gst_pad_get_parent (pad));

  structure = gst_caps_get_structure (caps, 0);

  gst_structure_get_double (structure, "framerate", &rfbsrc->framerate);

  return GST_PAD_LINK_OK;
}

static const GstQueryType *
gst_rfbsrc_get_query_types (GstPad * pad)
{
  static const GstQueryType query_types[] = {
    GST_QUERY_POSITION,
    0,
  };

  return query_types;
}

static gboolean
gst_rfbsrc_src_query (GstPad * pad,
    GstQueryType type, GstFormat * format, gint64 * value)
{
  gboolean res = FALSE;

  //GstRfbsrc *rfbsrc = GST_RFBSRC (gst_pad_get_parent (pad));

  switch (type) {
    case GST_QUERY_POSITION:
      switch (*format) {
        case GST_FORMAT_TIME:
          //*value = rfbsrc->n_frames * GST_SECOND / (double) rfbsrc->rate;
          res = TRUE;
          break;
        case GST_FORMAT_DEFAULT:       /* frames */
          //*value = rfbsrc->n_frames;
          res = TRUE;
          break;
        default:
          break;
      }
      break;
    default:
      break;
  }

  return res;
}

static gboolean
gst_rfbsrc_handle_src_event (GstPad * pad, GstEvent * event)
{
  GstRfbsrc *rfbsrc;
  double x, y;
  int button;
  GstStructure *structure;
  const char *event_type;

  rfbsrc = GST_RFBSRC (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NAVIGATION:
      structure = event->event_data.structure.structure;
      event_type = gst_structure_get_string (structure, "event");
      gst_structure_get_double (structure, "pointer_x", &x);
      gst_structure_get_double (structure, "pointer_y", &y);
      button = 0;

      if (strcmp (event_type, "key-press") == 0) {
        const char *key = gst_structure_get_string (structure, "key");

        rfb_decoder_send_key_event (rfbsrc->decoder, key[0], 1);
        rfb_decoder_send_key_event (rfbsrc->decoder, key[0], 0);
      } else if (strcmp (event_type, "mouse-move") == 0) {
        rfb_decoder_send_pointer_event (rfbsrc->decoder, rfbsrc->button_mask,
            (int) x, (int) y);
      } else if (strcmp (event_type, "mouse-button-release") == 0) {
        rfbsrc->button_mask &= ~(1 << button);
        rfb_decoder_send_pointer_event (rfbsrc->decoder, rfbsrc->button_mask,
            (int) x, (int) y);
      } else if (strcmp (event_type, "mouse-button-press") == 0) {
        rfbsrc->button_mask |= (1 << button);
        rfb_decoder_send_pointer_event (rfbsrc->decoder, rfbsrc->button_mask,
            (int) x, (int) y);
      }
      break;
    default:
      break;
  }

  return TRUE;
}

static void
gst_rfbsrc_paint_rect (RfbDecoder * decoder, int x, int y, int w, int h,
    guint8 * data)
{
  int i, j;
  guint8 *frame;
  GstRfbsrc *rfbsrc;
  int width;
  int offset;

  GST_DEBUG ("painting %d,%d (%dx%d)\n", x, y, w, h);
  rfbsrc = GST_RFBSRC (decoder->decoder_private);

  frame = rfbsrc->frame;
  width = decoder->width;
  switch (decoder->image_format) {
    case RFB_DECODER_IMAGE_RGB332:
      for (j = 0; j < h; j++) {
        for (i = 0; i < w; i++) {
          guint8 color;

          color = data[j * w + i];

#define RGB332_R(x)  ((((x)&0x07) * 0x124)>>3)
#define RGB332_G(x)  ((((x)&0x38) * 0x124)>>6)
#define RGB332_B(x)  ((((x)&0xc0) * 0x149)>>8)
          offset = ((j + y) * width + (i + x)) * 4;
          frame[offset + 0] = RGB332_B (color);
          frame[offset + 1] = RGB332_G (color);
          frame[offset + 2] = RGB332_R (color);
          frame[offset + 3] = 0;
        }
      }
      break;
    case RFB_DECODER_IMAGE_xRGB:
      for (j = 0; j < h; j++) {
        for (i = 0; i < w; i++) {
          guint32 color;

          color = ((guint32 *) data)[j * w + i];

#define xRGB_R(x)  (((x)&0xff0000)>>16)
#define xRGB_G(x)  (((x)&0x00ff00)>>8)
#define xRGB_B(x)  ((x)&0x0000ff)
          offset = ((j + y) * width + (i + x)) * 4;
          frame[offset + 0] = xRGB_B (color);
          frame[offset + 1] = xRGB_G (color);
          frame[offset + 2] = xRGB_R (color);
          frame[offset + 3] = 0;
        }
      }
      break;
    default:
      g_assert_not_reached ();
  }

  rfbsrc->go = FALSE;
}

static GstData *
gst_rfbsrc_get (GstPad * pad)
{
  GstRfbsrc *rfbsrc;
  gulong newsize;
  GstBuffer *buf;
  RfbDecoder *decoder;
  int ret;
  GstClockTime the_time = 0;
  GstClockTime delay;

  GST_DEBUG ("gst_rfbsrc_get");

  g_return_val_if_fail (pad != NULL, NULL);
  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  rfbsrc = GST_RFBSRC (gst_pad_get_parent (pad));
  decoder = rfbsrc->decoder;

  rfb_decoder_send_update_request (decoder, rfbsrc->inter, 0, 0, decoder->width,
      decoder->height);
  rfbsrc->inter = TRUE;

  delay = GST_SECOND / rfbsrc->framerate / 2;
  do {
    ret = gst_rfbsrc_handle_input (rfbsrc, 10000);

    ret = rfb_decoder_iterate (decoder);
    if (decoder->error_msg) {
      char *msg;

      msg = decoder->error_msg ? decoder->error_msg : "unknown";
      GST_ELEMENT_ERROR (rfbsrc, STREAM, TOO_LAZY,
          ("error in RFB decoder: %s", msg), (NULL));
      //return GST_DATA (gst_event_new (GST_EVENT_EOS));
      return NULL;
    }

    if (rfbsrc->clock) {
      the_time = gst_clock_get_time (rfbsrc->clock);
      if (rfbsrc->timestamp == GST_CLOCK_TIME_NONE) {
        rfbsrc->timestamp = the_time;
      }
    }
  } while (the_time < rfbsrc->timestamp + delay);

  newsize = decoder->width * decoder->height * 4;
  g_return_val_if_fail (newsize > 0, NULL);

  GST_DEBUG ("size=%ld %dx%d", newsize, decoder->width, decoder->height);

  buf = gst_buffer_new_and_alloc (newsize);
  g_return_val_if_fail (GST_BUFFER_DATA (buf) != NULL, NULL);
  GST_BUFFER_TIMESTAMP (buf) = rfbsrc->timestamp;
  GST_BUFFER_DURATION (buf) = GST_SECOND / rfbsrc->framerate;

  memcpy (GST_BUFFER_DATA (buf), rfbsrc->frame, newsize);

  rfbsrc->timestamp += GST_SECOND / rfbsrc->framerate;

  GST_ERROR ("pushing");
  return GST_DATA (buf);
}

static gboolean
gst_rfbsrc_handle_input (GstRfbsrc * rfbsrc, int timeout)
{
  fd_set readfds;
  struct timeval tv;
  int ret;

  GST_DEBUG ("enter");

  FD_ZERO (&readfds);
  FD_SET (rfbsrc->decoder->fd, &readfds);
  tv.tv_usec = timeout;
  tv.tv_sec = 0;
  ret = select (rfbsrc->decoder->fd + 1, &readfds, NULL, NULL, &tv);
  GST_DEBUG ("select returned %d", ret);
  if (ret > 0) {
    RfbBuffer *buffer;

    buffer = rfb_buffer_new_and_alloc (65536);
    ret = read (rfbsrc->decoder->fd, buffer->data, 65536);
    if (ret < 0) {
      g_warning ("FIXME read error");
    }
    buffer->length = ret;
    GST_DEBUG ("pushing buffer length %d", ret);
    rfb_buffer_queue_push (rfbsrc->decoder->queue, buffer);

    return TRUE;
  } else {
    GST_DEBUG ("timeout");
    return FALSE;
  }
}

static gboolean
gst_rfbsrc_connect_to_server (GstRfbsrc * rfbsrc)
{
  int n_timeouts;

  GST_DEBUG ("enter");

  rfbsrc->decoder = rfb_decoder_new ();
  rfb_decoder_connect_tcp (rfbsrc->decoder, rfbsrc->server, rfbsrc->port);
  rfbsrc->decoder->paint_rect = gst_rfbsrc_paint_rect;
  rfbsrc->decoder->decoder_private = rfbsrc;

  n_timeouts = 10;
  while (!rfbsrc->decoder->inited) {
    gboolean ret;

    ret = gst_rfbsrc_handle_input (rfbsrc, 10000);
    if (!ret) {
      n_timeouts--;
      if (n_timeouts == 0)
        return FALSE;
    }

    ret = rfb_decoder_iterate (rfbsrc->decoder);
    if (rfbsrc->decoder->error_msg) {
      char *msg;

      msg = rfbsrc->decoder->error_msg ? rfbsrc->decoder->error_msg : "unknown";
      GST_ELEMENT_ERROR (rfbsrc, STREAM, TOO_LAZY,
          ("error in RFB decoder: %s", msg), (NULL));
      return FALSE;
    }
  }

  if (rfbsrc->frame)
    g_free (rfbsrc->frame);

  rfbsrc->frame =
      g_malloc (rfbsrc->decoder->width * rfbsrc->decoder->height * 4);
  rfbsrc->inter = FALSE;
  rfbsrc->timestamp = GST_CLOCK_TIME_NONE;

  return TRUE;
}

static void
gst_rfbsrc_set_clock (GstElement * element, GstClock * clock)
{
  GstRfbsrc *rfbsrc = GST_RFBSRC (element);

  gst_object_replace ((GstObject **) & rfbsrc->clock, (GstObject *) clock);
}

static void
gst_rfbsrc_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GstRfbsrc *src;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_RFBSRC (object));
  src = GST_RFBSRC (object);

  GST_DEBUG ("gst_rfbsrc_set_property");
  switch (prop_id) {
    case ARG_SERVER:
      src->server = g_strdup (g_value_get_string (value));
      break;
    case ARG_PORT:
      src->port = g_value_get_int (value);
      break;
    default:
      break;
  }
}

static void
gst_rfbsrc_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstRfbsrc *src;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_RFBSRC (object));
  src = GST_RFBSRC (object);

  switch (prop_id) {
    case ARG_SERVER:
      g_value_set_string (value, src->server);
      break;
    case ARG_PORT:
      g_value_set_int (value, src->port);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


static gboolean
plugin_init (GstPlugin * plugin)
{
  gst_element_register (plugin, "rfbsrc", GST_RANK_NONE, GST_TYPE_RFBSRC);

  GST_DEBUG_CATEGORY_INIT (gst_debug_rfbsrc, "rfbsrc", 0, "rfbsrc element");

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "rfbsrc",
    "Connects to a VNC server and decodes RFB stream",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE, GST_ORIGIN)
