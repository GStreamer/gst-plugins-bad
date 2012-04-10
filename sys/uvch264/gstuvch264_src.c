/*
 * GStreamer
 *
 * Copyright (C) 201 Collabora Ltd.
 *   Author: Youness Alaoui <youness.alaoui@collabora.co.uk>
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


/**
 * SECTION:element-uvch264-src
 *
 * A camera bin src element that wraps v4l2src and implements UVC H264
 * Extension Units (XU) to control the H264 encoder in the camera
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "gstuvch264_src.h"

enum
{
  PROP_0,
};

GST_DEBUG_CATEGORY (uvc_h264_src_debug);
#define GST_CAT_DEFAULT uvc_h264_src_debug

GST_BOILERPLATE (GstUvcH264Src, gst_uvc_h264_src,
    GstBaseCameraSrc, GST_TYPE_BASE_CAMERA_SRC);

static void gst_uvc_h264_src_dispose (GObject * object);
static void gst_uvc_h264_src_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_uvc_h264_src_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static gboolean gst_uvc_h264_src_event (GstPad * pad, GstEvent * event);
static gboolean gst_uvc_h264_src_construct_pipeline (GstBaseCameraSrc *
    bcamsrc);
static gboolean gst_uvc_h264_src_set_mode (GstBaseCameraSrc * bcamsrc,
    GstCameraBinMode mode);
static gboolean gst_uvc_h264_src_start_capture (GstBaseCameraSrc * camerasrc);
static void gst_uvc_h264_src_stop_capture (GstBaseCameraSrc * camerasrc);
static GstStateChangeReturn gst_uvc_h264_src_change_state (GstElement * element,
    GstStateChange trans);

static void
gst_uvc_h264_src_base_init (gpointer g_class)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);

  GST_DEBUG_CATEGORY_INIT (uvc_h264_src_debug, "uvch264_src",
      0, "UVC H264 Compliant camera bin source");

  gst_element_class_set_details_simple (gstelement_class,
      "UVC H264 Source",
      "Source/Video",
      "UVC H264 Encoding camera source",
      "Youness Alaoui <youness.alaoui@collabora.co.uk>");
}

static void
gst_uvc_h264_src_class_init (GstUvcH264SrcClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseCameraSrcClass *gstbasecamerasrc_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gstelement_class = GST_ELEMENT_CLASS (klass);
  gstbasecamerasrc_class = GST_BASE_CAMERA_SRC_CLASS (klass);

  gobject_class->dispose = gst_uvc_h264_src_dispose;
  gobject_class->set_property = gst_uvc_h264_src_set_property;
  gobject_class->get_property = gst_uvc_h264_src_get_property;

  gstelement_class->change_state = gst_uvc_h264_src_change_state;

  gstbasecamerasrc_class->construct_pipeline =
      gst_uvc_h264_src_construct_pipeline;
  gstbasecamerasrc_class->set_mode = gst_uvc_h264_src_set_mode;
  gstbasecamerasrc_class->start_capture = gst_uvc_h264_src_start_capture;
  gstbasecamerasrc_class->stop_capture = gst_uvc_h264_src_stop_capture;
}

static void
gst_uvc_h264_src_init (GstUvcH264Src * self, GstUvcH264SrcClass * klass)
{
  self->vfsrc =
      gst_ghost_pad_new_no_target (GST_BASE_CAMERA_SRC_VIEWFINDER_PAD_NAME,
      GST_PAD_SRC);
  gst_element_add_pad (GST_ELEMENT (self), self->vfsrc);

  self->imgsrc =
      gst_ghost_pad_new_no_target (GST_BASE_CAMERA_SRC_IMAGE_PAD_NAME,
      GST_PAD_SRC);
  gst_element_add_pad (GST_ELEMENT (self), self->imgsrc);

  self->vidsrc =
      gst_ghost_pad_new_no_target (GST_BASE_CAMERA_SRC_VIDEO_PAD_NAME,
      GST_PAD_SRC);
  gst_element_add_pad (GST_ELEMENT (self), self->vidsrc);

  self->srcpad_event_func = GST_PAD_EVENTFUNC (self->vfsrc);

  gst_pad_set_event_function (self->imgsrc, gst_uvc_h264_src_event);
  gst_pad_set_event_function (self->vidsrc, gst_uvc_h264_src_event);
  gst_pad_set_event_function (self->vfsrc, gst_uvc_h264_src_event);

  self->auto_start = TRUE;
  gst_base_camera_src_set_mode (GST_BASE_CAMERA_SRC (self), MODE_VIDEO);
}

static void
gst_uvc_h264_src_dispose (GObject * object)
{
  GstUvcH264Src *self = GST_UVC_H264_SRC (object);

  (void) self;

  G_OBJECT_CLASS (parent_class)->dispose (object);
}


static void
gst_uvc_h264_src_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstUvcH264Src *self = GST_UVC_H264_SRC (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (self, prop_id, pspec);
      break;
  }
}

static void
gst_uvc_h264_src_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstUvcH264Src *self = GST_UVC_H264_SRC (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (self, prop_id, pspec);
      break;
  }
}


static gboolean
gst_uvc_h264_src_event (GstPad * pad, GstEvent * event)
{
  GstUvcH264Src *self = GST_UVC_H264_SRC (GST_PAD_PARENT (pad));
  const GstStructure *structure;

  structure = gst_event_get_structure (event);
  if (structure && gst_structure_has_name (structure, "renegotiate")) {
    GST_DEBUG_OBJECT (self, "Received renegotiate on %s", GST_PAD_NAME (pad));
  }

  return self->srcpad_event_func (pad, event);
}

static gboolean
gst_uvc_h264_src_construct_pipeline (GstBaseCameraSrc * bcamsrc)
{
  GstUvcH264Src *self = GST_UVC_H264_SRC (bcamsrc);
  GstPad *vf_pad = NULL;
  GstPad *h264_pad = NULL;

  GST_DEBUG_OBJECT (self, "Construct pipeline");

  if (self->v4l2_src == NULL) {
    self->v4l2_src = gst_element_factory_make ("v4l2src", NULL);
    self->mjpg_demux = gst_element_factory_make ("uvch264_mjpgdemux", NULL);
    self->jpeg_dec = gst_element_factory_make ("jpegdec", NULL);

    if (!self->v4l2_src || !self->mjpg_demux || !self->jpeg_dec)
      goto error;

    g_object_set (self->v4l2_src, "device", "/dev/video1", NULL);

    gst_bin_add_many (GST_BIN (self), self->v4l2_src,
        self->mjpg_demux, self->jpeg_dec, NULL);
    gst_object_ref (self->v4l2_src);
    gst_object_ref (self->mjpg_demux);
    gst_object_ref (self->jpeg_dec);

    if (!gst_element_link_filtered (self->v4l2_src, self->mjpg_demux,
            gst_caps_new_simple ("image/jpeg",
                "width", G_TYPE_INT, 320, "height", G_TYPE_INT, 240, NULL)))
      goto error_remove;
    if (!gst_element_link_pads (self->mjpg_demux, "jpeg", self->jpeg_dec, NULL))
      goto error_remove;

    h264_pad = gst_element_get_static_pad (self->mjpg_demux, "h264");
    vf_pad = gst_element_get_static_pad (self->jpeg_dec, "src");
    if (!h264_pad || !vf_pad)
      goto error_pads;

    gst_ghost_pad_set_target (GST_GHOST_PAD (self->vidsrc), h264_pad);
    gst_ghost_pad_set_target (GST_GHOST_PAD (self->vfsrc), vf_pad);
  }

  return TRUE;

error_pads:
  if (h264_pad)
    gst_object_unref (h264_pad);
  if (vf_pad)
    gst_object_unref (vf_pad);

error_remove:
  gst_bin_remove_many (GST_BIN (self), self->v4l2_src,
      self->mjpg_demux, self->jpeg_dec, NULL);
error:
  if (self->v4l2_src)
    gst_object_unref (self->v4l2_src);
  self->v4l2_src = NULL;

  if (self->mjpg_demux)
    gst_object_unref (self->mjpg_demux);
  self->mjpg_demux = NULL;

  if (self->jpeg_dec)
    gst_object_unref (self->jpeg_dec);
  self->jpeg_dec = NULL;

  return FALSE;
}

static gboolean
gst_uvc_h264_src_set_mode (GstBaseCameraSrc * bcamsrc, GstCameraBinMode mode)
{
  GstUvcH264Src *self = GST_UVC_H264_SRC (bcamsrc);

  GST_DEBUG_OBJECT (self, "set mode");

  return TRUE;
}

static gboolean
gst_uvc_h264_src_start_capture (GstBaseCameraSrc * camerasrc)
{
  GstUvcH264Src *self = GST_UVC_H264_SRC (camerasrc);

  GST_DEBUG_OBJECT (self, "start capture");
  return TRUE;
}

static void
gst_uvc_h264_src_stop_capture (GstBaseCameraSrc * camerasrc)
{
  GstUvcH264Src *self = GST_UVC_H264_SRC (camerasrc);

  GST_DEBUG_OBJECT (self, "stop capture");
}

static GstStateChangeReturn
gst_uvc_h264_src_change_state (GstElement * element, GstStateChange trans)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstUvcH264Src *self = GST_UVC_H264_SRC (element);

  switch (trans) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      /*  TODO: Check for H264 XU */
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      if (self->auto_start)
        g_signal_emit_by_name (G_OBJECT (self), "start-capture", NULL);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, trans);

  if (ret == GST_STATE_CHANGE_FAILURE)
    goto end;

  switch (trans) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      self->drop_newseg = FALSE;
      break;
    default:
      break;
  }


end:
  return ret;
}
