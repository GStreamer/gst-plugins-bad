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
 * Parses a MJPG stream from a UVC H264 compliant encoding camera and extracts
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

static GstFlowReturn gst_uvc_h264_mjpg_demux_chain (GstPad * pad,
    GstBuffer * buffer);
static gboolean gst_uvc_h264_mjpg_demux_sink_setcaps (GstPad * pad,
    GstCaps * caps);
static GstCaps *gst_uvc_h264_mjpg_demux_getcaps (GstPad * pad);

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
  GObjectClass *gobject_class = (GObjectClass *) klass;

  g_type_class_add_private (gobject_class, sizeof (GstUvcH264MjpgDemuxPrivate));
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

static GstFlowReturn
gst_uvc_h264_mjpg_demux_chain (GstPad * pad, GstBuffer * buf)
{
  GstUvcH264MjpgDemux *self;
  GstFlowReturn ret = GST_FLOW_OK;
  GstBufferList *jpeg_buf = gst_buffer_list_new ();
  GstBufferListIterator *jpeg_it = gst_buffer_list_iterate (jpeg_buf);
  GstBufferList *aux_buf = NULL;
  GstBufferListIterator *aux_it = NULL;
  AuxiliaryStreamHeader aux_header = { 0 };
  GstBuffer *sub_buffer = NULL;
  guint32 aux_size = 0;
  GstPad *aux_pad = NULL;
  GstCaps *aux_caps = NULL;
  guint last_offset;
  guint i;
  guchar *data;
  guint size;

  self = GST_UVC_H264_MJPG_DEMUX (GST_PAD_PARENT (pad));

  last_offset = 0;
  data = GST_BUFFER_DATA (buf);
  size = GST_BUFFER_SIZE (buf);
  gst_buffer_list_iterator_add_group (jpeg_it);
  for (i = 0; i < size - 1; i++) {
    /* Check for APP4 (0xe4) marker in the jpeg */
    if (data[i] == 0xff && data[i + 1] == 0xe4) {
      guint16 segment_size;

      /* Sanity check sizes and get segment size */
      if (i + 4 >= size) {
        GST_ELEMENT_ERROR (self, STREAM, DEMUX,
            ("Not enough data to read marker size"), (NULL));
        ret = GST_FLOW_ERROR;
        goto done;
      }
      segment_size = GUINT16_FROM_BE (*((guint16 *) (data + i + 2)));

      if (i + segment_size + 2 >= size) {
        GST_ELEMENT_ERROR (self, STREAM, DEMUX,
            ("Not enough data to read marker content"), (NULL));
        ret = GST_FLOW_ERROR;
        goto done;
      }
      GST_DEBUG_OBJECT (self,
          "Found APP4 marker (%d). JPG: %d-%d - APP4: %d - %d", segment_size,
          last_offset, i, i, i + 2 + segment_size);

      /* Add JPEG data between the last offset and this market */
      if (i - last_offset > 0) {
        sub_buffer = gst_buffer_create_sub (buf, last_offset, i - last_offset);
        gst_buffer_copy_metadata (sub_buffer, buf, GST_BUFFER_COPY_ALL);
        gst_buffer_list_iterator_add (jpeg_it, sub_buffer);
      }
      last_offset = i + 2 + segment_size - 1;

      /* Reset i/segment size to the app4 data (ignore marker header/size) */
      i += 4;
      segment_size -= 2;

      /* If this is a new auxiliary stream, initialize everything properly */
      if (aux_buf == NULL) {
        if (segment_size < sizeof (aux_header) + sizeof (aux_size)) {
          GST_ELEMENT_ERROR (self, STREAM, DEMUX,
              ("Not enough data to read aux header"), (NULL));
          ret = GST_FLOW_ERROR;
          goto done;
        }

        aux_header = *((AuxiliaryStreamHeader *) (data + i));
        /* version should be little endian but it looks more like BE */
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
        aux_size = *((guint32 *) (data + i + aux_header.header_len));
        GST_DEBUG_OBJECT (self, "Auxiliary stream size : %d bytes", aux_size);

        /* Find the auxiliary stream's pad and caps */
        switch (aux_header.type) {
          case GST_MAKE_FOURCC ('H', '2', '6', '4'):
            aux_pad = self->priv->h264_pad;
            aux_caps = gst_caps_new_simple ("video/x-h264", NULL);
            break;
          case GST_MAKE_FOURCC ('Y', 'U', 'Y', '2'):
            aux_pad = self->priv->yuy2_pad;
            aux_caps = gst_caps_new_simple ("video/x-raw-yuv",
                "format", GST_TYPE_FOURCC, aux_header.type, NULL);
            break;
          case GST_MAKE_FOURCC ('N', 'V', '1', '2'):
            aux_pad = self->priv->nv12_pad;
            aux_caps = gst_caps_new_simple ("video/x-raw-yuv",
                "format", GST_TYPE_FOURCC, aux_header.type, NULL);
            break;
          default:
            GST_ELEMENT_ERROR (self, STREAM, DEMUX,
                ("Unknown auxiliary stream format : %" GST_FOURCC_FORMAT,
                    GST_FOURCC_ARGS (aux_header.type)), (NULL));
            ret = GST_FLOW_ERROR;
            break;
        }

        if (ret != GST_FLOW_OK)
          goto done;

        gst_caps_set_simple (aux_caps,
            "width", G_TYPE_INT, aux_header.width,
            "height", G_TYPE_INT, aux_header.height,
            "framerate", GST_TYPE_FRACTION,
            1000000000 / aux_header.frame_interval, 100, NULL);

        /* Create new auxiliary buffer list and adjust i/segment size */
        aux_buf = gst_buffer_list_new ();
        aux_it = gst_buffer_list_iterate (aux_buf);
        gst_buffer_list_iterator_add_group (aux_it);

        i += sizeof (aux_header) + sizeof (aux_size);
        segment_size -= sizeof (aux_header) + sizeof (aux_size);
      }

      sub_buffer = gst_buffer_create_sub (buf, i, segment_size);
      /* TODO: Transform PTS into proper buffer timestamp */
      //GST_BUFFER_TIMESTAMP (aux_buffer) = aux_header.pts;
      gst_buffer_copy_metadata (sub_buffer, buf, GST_BUFFER_COPY_TIMESTAMPS);
      gst_buffer_set_caps (sub_buffer, aux_caps);
      gst_buffer_list_iterator_add (aux_it, sub_buffer);

      if (segment_size > aux_size) {
        GST_ELEMENT_ERROR (self, STREAM, DEMUX,
            ("Expected %d auxiliary data, got %d bytes", aux_size,
                segment_size), (NULL));
        ret = GST_FLOW_ERROR;
        goto done;
      }
      aux_size -= segment_size;

      /* Push completed aux data */
      if (aux_size == 0) {
        gst_buffer_list_iterator_free (aux_it);
        aux_it = NULL;
        GST_DEBUG_OBJECT (self, "Pushing %" GST_FOURCC_FORMAT
            " auxiliary buffer %" GST_PTR_FORMAT,
            GST_FOURCC_ARGS (aux_header.type), aux_caps);
        ret = gst_pad_push_list (aux_pad, aux_buf);
        aux_buf = NULL;
        if (ret != GST_FLOW_OK) {
          GST_WARNING_OBJECT (self, "Error pushing %" GST_FOURCC_FORMAT
              " auxiliary data", GST_FOURCC_ARGS (aux_header.type));
          goto done;
        }
      }

      i += segment_size - 1;
    } else if (data[i] == 0xff && data[i + 1] == 0xda) {

      /* The APP4 markers must be before the SOS marker, so this is the end */
      GST_DEBUG_OBJECT (self, "Found SOS marker.");

      sub_buffer = gst_buffer_create_sub (buf, last_offset, size - last_offset);
      gst_buffer_copy_metadata (sub_buffer, buf, GST_BUFFER_COPY_ALL);
      gst_buffer_list_iterator_add (jpeg_it, sub_buffer);
      last_offset = size;
      break;
    }
  }
  gst_buffer_list_iterator_free (jpeg_it);
  jpeg_it = NULL;

  if (aux_buf != NULL) {
    GST_ELEMENT_ERROR (self, STREAM, DEMUX,
        ("Incomplete auxiliary stream. %d bytes missing", aux_size), (NULL));
    ret = GST_FLOW_ERROR;
    goto done;
  }

  if (last_offset != size) {
    /* this means there was no SOS marker in the jpg, so we assume the JPG was
       just a container */
    GST_DEBUG_OBJECT (self, "SOS marker wasn't found. MJPG is container only");
    gst_buffer_list_unref (jpeg_buf);
    jpeg_buf = NULL;
  } else {
    ret = gst_pad_push_list (self->priv->jpeg_pad, jpeg_buf);
    jpeg_buf = NULL;
  }

  if (ret != GST_FLOW_OK) {
    GST_WARNING_OBJECT (self, "Error pushing jpeg data");
    goto done;
  }

done:
  /* In case of error, unref whatever was left */
  if (aux_it)
    gst_buffer_list_iterator_free (aux_it);
  if (aux_buf)
    gst_buffer_list_unref (aux_buf);
  if (jpeg_it)
    gst_buffer_list_iterator_free (jpeg_it);
  if (jpeg_buf)
    gst_buffer_list_unref (jpeg_buf);

  /* We must always unref the input buffer since we never push it out */
  gst_buffer_unref (buf);

  return ret;
}
