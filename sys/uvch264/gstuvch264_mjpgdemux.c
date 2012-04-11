/* GStreamer
 *
 * uvch264_mjpg_demux: a demuxer for muxed stream in UVC H264 compliant MJPG
 *
 * Copyright (C) <2012> Collabora Ltd.
 *   Author: Youness Alaoui <youness.alaoui@collabor.co.uk>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/**
 * SECTION:element-uvch264-mjpgdemux
 * @short_description: UVC H264 compliant MJPG demuxer
 *
 * Parses a MJPG stream from a UVC H264 compliant encoding camera and extracts the
 * each muxed stream into separate pads.
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>

#include "gstuvch264_mjpgdemux.h"

static GstStaticPadTemplate gst_uvc_h264_mjpg_demux_sink_pad_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("image/jpeg, "
        "width = (int) [ 0, MAX ],"
        "height = (int) [ 0, MAX ], " "framerate = (fraction) [ 0/1, MAX ] ")
    );

static GstStaticPadTemplate gst_uvc_h264_mjpg_demux_jpegsrc_pad_template =
GST_STATIC_PAD_TEMPLATE ("jpeg",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("image/jpeg, "
        "width = (int) [ 0, MAX ],"
        "height = (int) [ 0, MAX ], " "framerate = (fraction) [ 0/1, MAX ] ")
    );

static GstStaticPadTemplate gst_uvc_h264_mjpg_demux_h264src_pad_template =
GST_STATIC_PAD_TEMPLATE ("h264",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h264, "
        "width = (int) [ 0, MAX ], "
        "height = (int) [ 0, MAX ], " "framerate = (fraction) [ 0/1, MAX ] ")
    );

static GstStaticPadTemplate gst_uvc_h264_mjpg_demux_yuy2src_pad_template =
GST_STATIC_PAD_TEMPLATE ("yuy2",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw-yuv, "
        "format = (fourcc) YUY2, "
        "width = (int) [ 0, MAX ], "
        "height = (int) [ 0, MAX ], " "framerate = (fraction) [ 0/1, MAX ] ")
    );
static GstStaticPadTemplate gst_uvc_h264_mjpg_demux_nv12src_pad_template =
GST_STATIC_PAD_TEMPLATE ("nv12",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw-yuv, "
        "format = (fourcc) NV21, "
        "width = (int) [ 0, MAX ], "
        "height = (int) [ 0, MAX ], " "framerate = (fraction) [ 0/1, MAX ] ")
    );


GST_DEBUG_CATEGORY_STATIC (uvc_h264_mjpg_demux_debug);
#define GST_CAT_DEFAULT uvc_h264_mjpg_demux_debug

struct _GstUvcH264MjpgDemuxPrivate
{
  GstPad *sink_pad;
  GstPad *jpeg_pad;
  GstPad *h264_pad;
  GstPad *yuy2_pad;
  GstPad *nv12_pad;

  gchar *app4_buffer;
  guint app4_size;
  guint app4_len;
};

typedef struct
{
  guint16 version;
  guint16 header_len;
  guint32 type;
  guint16 width;
  guint16 height;
  guint32 frame_interval;
  guint16 delay;
  guint32 pts;
} __attribute__ ((packed)) AuxiliaryStreamHeader;

static void gst_uvc_h264_mjpg_demux_dispose (GObject * object);

static GstFlowReturn gst_uvc_h264_mjpg_demux_chain (GstPad * pad,
    GstBuffer * buffer);
static gboolean gst_uvc_h264_mjpg_demux_sink_setcaps (GstPad * pad,
    GstCaps * caps);
static GstCaps *gst_uvc_h264_mjpg_demux_getcaps (GstPad * pad);
static GstStateChangeReturn gst_uvc_h264_mjpg_demux_change_state (GstElement *
    element, GstStateChange transition);

#define _do_init(x) \
  GST_DEBUG_CATEGORY_INIT (uvc_h264_mjpg_demux_debug, \
      "uvch264_mjpgdemux", 0, "UVC H264 MJPG Demuxer");

GST_BOILERPLATE_FULL (GstUvcH264MjpgDemux, gst_uvc_h264_mjpg_demux, GstElement,
    GST_TYPE_ELEMENT, _do_init);

static void
gst_uvc_h264_mjpg_demux_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_static_pad_template (element_class,
      &gst_uvc_h264_mjpg_demux_sink_pad_template);
  gst_element_class_add_static_pad_template (element_class,
      &gst_uvc_h264_mjpg_demux_jpegsrc_pad_template);
  gst_element_class_add_static_pad_template (element_class,
      &gst_uvc_h264_mjpg_demux_h264src_pad_template);
  gst_element_class_add_static_pad_template (element_class,
      &gst_uvc_h264_mjpg_demux_yuy2src_pad_template);
  gst_element_class_add_static_pad_template (element_class,
      &gst_uvc_h264_mjpg_demux_nv12src_pad_template);
  gst_element_class_set_details_simple (element_class,
      "UVC H264 MJPG Demuxer",
      "Video/Demuxer",
      "Demux UVC H264 auxiliary streams from MJPG images",
      "Youness Alaoui <youness.alaoui@collabora.co.uk>");
}

static void
gst_uvc_h264_mjpg_demux_class_init (GstUvcH264MjpgDemuxClass * klass)
{
  GstElementClass *gstelement_class;
  GObjectClass *gobject_class;

  gstelement_class = (GstElementClass *) klass;
  gobject_class = (GObjectClass *) klass;

  g_type_class_add_private (gobject_class, sizeof (GstUvcH264MjpgDemuxPrivate));
  gobject_class->dispose = gst_uvc_h264_mjpg_demux_dispose;

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_uvc_h264_mjpg_demux_change_state);
}

static void
gst_uvc_h264_mjpg_demux_init (GstUvcH264MjpgDemux * self,
    GstUvcH264MjpgDemuxClass * g_class)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, GST_TYPE_UVC_H264_MJPG_DEMUX,
      GstUvcH264MjpgDemuxPrivate);

  /* create the sink and src pads */
  self->priv->sink_pad =
      gst_pad_new_from_static_template
      (&gst_uvc_h264_mjpg_demux_sink_pad_template, "sink");
  gst_pad_set_chain_function (self->priv->sink_pad,
      GST_DEBUG_FUNCPTR (gst_uvc_h264_mjpg_demux_chain));
  gst_pad_set_setcaps_function (self->priv->sink_pad,
      GST_DEBUG_FUNCPTR (gst_uvc_h264_mjpg_demux_sink_setcaps));
  gst_pad_set_getcaps_function (self->priv->sink_pad,
      GST_DEBUG_FUNCPTR (gst_uvc_h264_mjpg_demux_getcaps));
  gst_element_add_pad (GST_ELEMENT (self), self->priv->sink_pad);

  /* JPEG */
  self->priv->jpeg_pad =
      gst_pad_new_from_static_template
      (&gst_uvc_h264_mjpg_demux_jpegsrc_pad_template, "jpeg");
  gst_pad_set_getcaps_function (self->priv->jpeg_pad,
      GST_DEBUG_FUNCPTR (gst_uvc_h264_mjpg_demux_getcaps));
  gst_element_add_pad (GST_ELEMENT (self), self->priv->jpeg_pad);

  /* H264 */
  self->priv->h264_pad =
      gst_pad_new_from_static_template
      (&gst_uvc_h264_mjpg_demux_h264src_pad_template, "h264");
  gst_pad_use_fixed_caps (self->priv->h264_pad);
  gst_element_add_pad (GST_ELEMENT (self), self->priv->h264_pad);

  /* YUY2 */
  self->priv->yuy2_pad =
      gst_pad_new_from_static_template
      (&gst_uvc_h264_mjpg_demux_yuy2src_pad_template, "yuy2");
  gst_pad_use_fixed_caps (self->priv->yuy2_pad);
  gst_element_add_pad (GST_ELEMENT (self), self->priv->yuy2_pad);

  /* NV12 */
  self->priv->nv12_pad =
      gst_pad_new_from_static_template
      (&gst_uvc_h264_mjpg_demux_nv12src_pad_template, "nv12");
  gst_pad_use_fixed_caps (self->priv->nv12_pad);
  gst_element_add_pad (GST_ELEMENT (self), self->priv->nv12_pad);

  self->priv->app4_buffer = NULL;
  self->priv->app4_size = 0;
  self->priv->app4_len = 0;
}

static void
gst_uvc_h264_mjpg_demux_dispose (GObject * object)
{
  GstUvcH264MjpgDemux *self = GST_UVC_H264_MJPG_DEMUX (object);

  if (self->priv->app4_buffer != NULL) {
    g_free (self->priv->app4_buffer);
    self->priv->app4_buffer = NULL;
    self->priv->app4_size = 0;
    self->priv->app4_len = 0;
  }
  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static gboolean
gst_uvc_h264_mjpg_demux_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  GstUvcH264MjpgDemux *self = GST_UVC_H264_MJPG_DEMUX (GST_OBJECT_PARENT (pad));

  return gst_pad_set_caps (self->priv->jpeg_pad, caps);
}

static GstCaps *
gst_uvc_h264_mjpg_demux_getcaps (GstPad * pad)
{
  GstUvcH264MjpgDemux *self = GST_UVC_H264_MJPG_DEMUX (GST_OBJECT_PARENT (pad));
  GstCaps *result = NULL;

  if (pad == self->priv->jpeg_pad)
    result = gst_pad_peer_get_caps (self->priv->sink_pad);
  else if (pad == self->priv->sink_pad)
    result = gst_pad_peer_get_caps (self->priv->jpeg_pad);

  if (result == NULL)
    result = gst_caps_copy (gst_pad_get_pad_template_caps (pad));

  return result;
}

static void
gst_uvc_h264_mjpg_add_app4 (GstUvcH264MjpgDemux * self, guchar * data,
    guint16 size)
{
  if (self->priv->app4_len + size > self->priv->app4_size) {
    self->priv->app4_size += 64 * 1024;
    self->priv->app4_buffer = g_realloc (self->priv->app4_buffer,
        self->priv->app4_size);
  }
  memcpy (self->priv->app4_buffer + self->priv->app4_len, data, size);
  self->priv->app4_len += size;
}

static GstFlowReturn
gst_uvc_h264_mjpg_demux_chain (GstPad * pad, GstBuffer * buf)
{
  GstUvcH264MjpgDemux *self;
  GstFlowReturn ret = GST_FLOW_OK;
  guint last_offset;
  guint i;
  guchar *data;
  guint size;
  GstBufferList *jpeg_buf = gst_buffer_list_new ();
  GstBufferListIterator *jpeg_it = gst_buffer_list_iterate (jpeg_buf);

  self = GST_UVC_H264_MJPG_DEMUX (GST_PAD_PARENT (pad));

  /* FIXME: should actually read each marker and skip its content */
  last_offset = 0;
  data = GST_BUFFER_DATA (buf);
  size = GST_BUFFER_SIZE (buf);
  gst_buffer_list_iterator_add_group (jpeg_it);
  for (i = 0; i < size - 1; i++) {
    if (data[i] == 0xff && data[i + 1] == 0xe4) {
      guint16 segment_size;

      if (i + 4 >= size) {
        GST_ERROR_OBJECT (self, "Not enough data to read marker size");
        ret = GST_FLOW_UNEXPECTED;
        goto error;
      }
      segment_size = GUINT16_FROM_BE (*((guint16 *) (data + i + 2)));

      if (i + segment_size + 2 >= size) {
        GST_ERROR_OBJECT (self, "Not enough data to read marker content");
        ret = GST_FLOW_UNEXPECTED;
        goto error;
      }
      GST_DEBUG_OBJECT (self,
          "Found APP4 marker (%d). JPG: %d-%d - APP4: %d - %d", segment_size,
          last_offset, i, i, i + 2 + segment_size);
      gst_uvc_h264_mjpg_add_app4 (self, data + i + 4, segment_size - 2);

      gst_buffer_list_iterator_add (jpeg_it,
          gst_buffer_create_sub (buf, last_offset, i - last_offset));
      i += 2 + segment_size - 1;
      last_offset = i;
    } else if (data[i] == 0xff && data[i + 1] == 0xda) {
      GST_DEBUG_OBJECT (self, "Found SOS marker.");
      /* The APP4 markers must be before the SOS marker, so this is the end */

      gst_buffer_list_iterator_add (jpeg_it,
          gst_buffer_create_sub (buf, last_offset, size - last_offset));
      last_offset = size;
      break;
    }
  }
  gst_buffer_list_iterator_free (jpeg_it);

  if (last_offset != size) {
    /* This shouldn't happen, would mean there was no SOS marker in the jpg */
    GST_ERROR_OBJECT (self, "SOS marker wasn't found");
    ret = GST_FLOW_UNEXPECTED;
    goto error;
  }
  ret = gst_pad_push_list (self->priv->jpeg_pad, jpeg_buf);

  if (ret != GST_FLOW_OK) {
    GST_WARNING_OBJECT (self, "Error pushing jpeg data");
    goto error;
  }

  /* TODO: do this at the same time as parsing, use sub buffers, buffers lists
   * and no memcpy */
  i = 0;
  while (i < self->priv->app4_len) {
    AuxiliaryStreamHeader aux_header;
    guint32 aux_size;
    GstPad *aux_pad = NULL;
    GstCaps *caps = NULL;

    GST_DEBUG_OBJECT (self, "Parsing APP4 data of size %d (offset %d)",
        self->priv->app4_len, i);

    aux_header = *((AuxiliaryStreamHeader *) (self->priv->app4_buffer + i));
    aux_header.version = GUINT16_FROM_BE (aux_header.version);
    aux_header.header_len = GUINT16_FROM_LE (aux_header.header_len);
    aux_header.width = GUINT16_FROM_LE (aux_header.width);
    aux_header.height = GUINT16_FROM_LE (aux_header.height);
    aux_header.frame_interval = GUINT32_FROM_LE (aux_header.frame_interval);
    aux_header.delay = GUINT16_FROM_LE (aux_header.delay);
    aux_header.pts = GUINT32_FROM_LE (aux_header.pts);
    GST_DEBUG_OBJECT (self, "New auxiliary stream : v%d - %d bytes - %"
        GST_FOURCC_FORMAT " %dx%d -- %d *100ns -- %d ms -- %d",
        aux_header.version, aux_header.header_len,
        GST_FOURCC_ARGS (aux_header.type),
        aux_header.width, aux_header.height,
        aux_header.frame_interval, aux_header.delay, aux_header.pts);
    aux_size = *((guint32 *)
        (self->priv->app4_buffer + i + aux_header.header_len));
    GST_DEBUG_OBJECT (self, "Auxiliary stream size : %d bytes", aux_size);

    i += aux_header.header_len + sizeof (guint32);
    if (aux_size > self->priv->app4_len - i) {
      GST_ERROR_OBJECT (self,
          "Not enough APP4 data for current auxiliary stream");
      ret = GST_FLOW_UNEXPECTED;
      goto error;
    }

    switch (aux_header.type) {
      case GST_MAKE_FOURCC ('H', '2', '6', '4'):
        aux_pad = self->priv->h264_pad;
        caps = gst_caps_new_simple ("video/x-h264", NULL);
        break;
      case GST_MAKE_FOURCC ('Y', 'U', 'Y', '2'):
        aux_pad = self->priv->yuy2_pad;
        caps = gst_caps_new_simple ("video/x-raw-yuv",
            "format", GST_TYPE_FOURCC, aux_header.type, NULL);
        break;
      case GST_MAKE_FOURCC ('N', 'V', '1', '2'):
        aux_pad = self->priv->nv12_pad;
        caps = gst_caps_new_simple ("video/x-raw-yuv",
            "format", GST_TYPE_FOURCC, aux_header.type, NULL);
        break;
      default:
        GST_WARNING_OBJECT (self, "Unknown auxiliary stream format : %"
            GST_FOURCC_FORMAT, GST_FOURCC_ARGS (aux_header.type));
        ret = GST_FLOW_UNEXPECTED;
        goto error;
        break;
    }

    if (caps != NULL && aux_pad != NULL) {
      GstBuffer *aux_buffer = NULL;

      gst_caps_set_simple (caps,
          "width", G_TYPE_INT, aux_header.width,
          "height", G_TYPE_INT, aux_header.height,
          "framerate", GST_TYPE_FRACTION,
          1000000000 / aux_header.frame_interval, 100, NULL);

      ret = gst_pad_alloc_buffer (aux_pad, 0, aux_size, caps, &aux_buffer);
      if (ret != GST_FLOW_OK) {
        GST_WARNING_OBJECT (self,
            "Could not pad_alloc buffer, mem allocating it instead");
        aux_buffer = gst_buffer_new_and_alloc (aux_size);
        GST_BUFFER_SIZE (aux_buffer) = aux_size;
      }
      memcpy (GST_BUFFER_DATA (aux_buffer), self->priv->app4_buffer + i,
          aux_size);
      gst_buffer_set_caps (aux_buffer, caps);

      /* TODO: Transform PTS into proper buffer timestamp */
      //GST_BUFFER_TIMESTAMP (aux_buffer) = aux_header.pts;
      gst_buffer_copy_metadata (aux_buffer, buf, GST_BUFFER_COPY_TIMESTAMPS);
      GST_DEBUG_OBJECT (self, "Pushing auxiliary buffer %" GST_PTR_FORMAT,
          caps);
      ret = gst_pad_push (aux_pad, aux_buffer);
      if (ret != GST_FLOW_OK) {
        GST_ERROR_OBJECT (self, "Error pushing auxiliary stream");
        goto error;
      }
    }
    i += aux_size;
  }
  self->priv->app4_len = 0;

error:
  return ret;
}

static GstStateChangeReturn
gst_uvc_h264_mjpg_demux_change_state (GstElement * element,
    GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstUvcH264MjpgDemux *self;

  self = GST_UVC_H264_MJPG_DEMUX (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      /* Initialize the buffer to 64K */
      self->priv->app4_buffer = g_malloc (64 * 1024);
      self->priv->app4_size = 64 * 1024;
      self->priv->app4_len = 0;
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret != GST_STATE_CHANGE_SUCCESS)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      if (self->priv->app4_buffer != NULL) {
        g_free (self->priv->app4_buffer);
        self->priv->app4_buffer = NULL;
        self->priv->app4_size = 0;
        self->priv->app4_len = 0;
      }
      break;
    default:
      break;
  }

  return ret;
}
