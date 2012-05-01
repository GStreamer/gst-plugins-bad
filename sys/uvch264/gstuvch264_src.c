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

#include <linux/uvcvideo.h>
#include <sys/ioctl.h>

#include "gstuvch264_src.h"

enum
{
  PROP_0,
  /* Static controls */
  PROP_INITIAL_BITRATE,
  PROP_SLICE_UNITS,
  PROP_SLICE_MODE,
  PROP_IFRAME_PERIOD,
  PROP_USAGE_TYPE,
  PROP_STREAM_FORMAT,
  PROP_ENTROPY,
  PROP_ENABLE_SEI,
  PROP_NUM_REORDER_FRAMES,
  PROP_PREVIEW_FLIPPED,
  /* Dynamic controls */
  PROP_RATE_CONTROL,
  PROP_FIXED_FRAMERATE,
  PROP_MAX_MBPS,                /* read-only */
  PROP_LEVEL_IDC,
  PROP_PEAK_BITRATE,
  PROP_AVERAGE_BITRATE,
  PROP_MIN_QP,
  PROP_MAX_QP,
  PROP_MIN_IFRAME_QP,
  PROP_MAX_IFRAME_QP,
  PROP_MIN_PFRAME_QP,
  PROP_MAX_PFRAME_QP,
  PROP_MIN_BFRAME_QP,
  PROP_MAX_BFRAME_QP,
};
/* In caps : frame interval (fps), width, height, profile, mux */
/* Ignored: temporal, spatial, SNR, MVC views, version, reset */
/* Events: LTR, generate IDR */

/* Default values */
#define DEFAULT_INITIAL_BITRATE 3000000
#define DEFAULT_SLICE_UNITS 4
#define DEFAULT_SLICE_MODE UVC_H264_SLICEMODE_SLICEPERFRAME
#define DEFAULT_IFRAME_PERIOD 10000
#define DEFAULT_USAGE_TYPE UVC_H264_USAGETYPE_REALTIME
#define DEFAULT_STREAM_FORMAT UVC_H264_STREAMFORMAT_ANNEXB
#define DEFAULT_ENTROPY UVC_H264_ENTROPY_CAVLC
#define DEFAULT_ENABLE_SEI FALSE
#define DEFAULT_NUM_REORDER_FRAMES 0
#define DEFAULT_PREVIEW_FLIPPED FALSE
#define DEFAULT_RATE_CONTROL UVC_H264_RATECONTROL_CBR
#define DEFAULT_FIXED_FRAMERATE FALSE
#define DEFAULT_LEVEL_IDC 0     /* FIXME: check real default */
#define DEFAULT_PEAK_BITRATE DEFAULT_INITIAL_BITRATE
#define DEFAULT_AVERAGE_BITRATE DEFAULT_INITIAL_BITRATE
#define DEFAULT_MIN_QP 0        /* FIXME: check real default */
#define DEFAULT_MAX_QP 0        /* FIXME: check real default */

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
static void fill_probe_commit_t (GstUvcH264Src * self,
    uvcx_video_config_probe_commit_t * probe, guint32 frame_interval,
    guint32 width, guint32 height, guint32 profile);
static gboolean xu_query (GstUvcH264Src * self, guint selector, guint query,
    guchar * data);

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

  /* TODO: Add device/device-name properties and the proprety probe interface */

  /* Properties */
  g_object_class_install_property (gobject_class, PROP_INITIAL_BITRATE,
      g_param_spec_uint ("initial-bitrate", "Initial bitrate",
          "Initial bitrate in bits/second (static control)",
          0, G_MAXUINT, DEFAULT_INITIAL_BITRATE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_SLICE_UNITS,
      g_param_spec_uint ("slice-units", "Slice units",
          "Slice units (static control)",
          0, G_MAXUINT16, DEFAULT_SLICE_UNITS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_SLICE_MODE,
      g_param_spec_enum ("slice-mode", "Slice mode",
          "Defines the unit of the slice-units property (static control)",
          UVC_H264_SLICEMODE_TYPE,
          DEFAULT_SLICE_MODE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_IFRAME_PERIOD,
      g_param_spec_uint ("iframe-period", "I Frame Period",
          "Time between IDR frames in milliseconds (static control)",
          0, G_MAXUINT16, DEFAULT_IFRAME_PERIOD,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_USAGE_TYPE,
      g_param_spec_enum ("usage-type", "Usage type",
          "The usage type (static control)",
          UVC_H264_USAGETYPE_TYPE, DEFAULT_USAGE_TYPE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_STREAM_FORMAT,
      g_param_spec_enum ("stream-format", "Stream format",
          "Stream format (static control)",
          UVC_H264_STREAMFORMAT_TYPE, DEFAULT_STREAM_FORMAT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_ENTROPY,
      g_param_spec_enum ("entropy", "Entropy",
          "Entropy (static control)",
          UVC_H264_ENTROPY_TYPE, DEFAULT_ENTROPY,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_ENABLE_SEI,
      g_param_spec_boolean ("enable-sei", "Enable SEI",
          "Enable SEI picture timing (static control)",
          DEFAULT_ENABLE_SEI, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_NUM_REORDER_FRAMES,
      g_param_spec_uint ("num-reorder-frames", "Number of Reorder frames",
          "Number of B frames between the references frames (static control)",
          0, G_MAXUINT8, DEFAULT_NUM_REORDER_FRAMES,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_PREVIEW_FLIPPED,
      g_param_spec_boolean ("preview-flipped", "Flip preview",
          "Horizontal flipped image for non H.264 streams (static control)",
          DEFAULT_PREVIEW_FLIPPED, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /* Dynamic controls */
  g_object_class_install_property (gobject_class, PROP_RATE_CONTROL,
      g_param_spec_enum ("rate-control", "Rate control",
          "Stream format (dynamic control)",
          UVC_H264_RATECONTROL_TYPE, DEFAULT_RATE_CONTROL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_FIXED_FRAMERATE,
      g_param_spec_boolean ("fixed-framerate", "Fixed framerate",
          "Fixed framerate (dynamic control)",
          DEFAULT_FIXED_FRAMERATE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_MAX_MBPS,
      g_param_spec_uint ("max-mbps", "Max macroblocks/second",
          "The number of macroblocks per second for the maximum processing rate",
          0, G_MAXUINT, 0, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_LEVEL_IDC,
      g_param_spec_uint ("level-idc", "Level IDC",
          "Level IDC (dynamic control)",
          0, G_MAXUINT8, DEFAULT_LEVEL_IDC,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_PEAK_BITRATE,
      g_param_spec_uint ("peak-bitrate", "Peak bitrate",
          "The peak bitrate in bits/second (dynamic control)",
          0, G_MAXUINT, DEFAULT_PEAK_BITRATE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_AVERAGE_BITRATE,
      g_param_spec_uint ("average-bitrate", "Average bitrate",
          "The average bitrate in bits/second (dynamic control)",
          0, G_MAXUINT, DEFAULT_AVERAGE_BITRATE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_MIN_IFRAME_QP,
      g_param_spec_int ("min-iframe-qp", "Minimum I frame QP",
          "The minimum Quantization step size for I frames (dynamic control)",
          -G_MAXINT8, G_MAXINT8, DEFAULT_MIN_QP,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_MAX_IFRAME_QP,
      g_param_spec_int ("max-iframe-qp", "Minimum I frame QP",
          "The minimum Quantization step size for I frames (dynamic control)",
          -G_MAXINT8, G_MAXINT8, DEFAULT_MAX_QP,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_MIN_PFRAME_QP,
      g_param_spec_int ("min-pframe-qp", "Minimum P frame QP",
          "The minimum Quantization step size for P frames (dynamic control)",
          -G_MAXINT8, G_MAXINT8, DEFAULT_MIN_QP,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_MAX_PFRAME_QP,
      g_param_spec_int ("max-pframe-qp", "Minimum P frame QP",
          "The minimum Quantization step size for P frames (dynamic control)",
          -G_MAXINT8, G_MAXINT8, DEFAULT_MAX_QP,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_MIN_BFRAME_QP,
      g_param_spec_int ("min-bframe-qp", "Minimum B frame QP",
          "The minimum Quantization step size for B frames (dynamic control)",
          -G_MAXINT8, G_MAXINT8, DEFAULT_MIN_QP,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_MAX_BFRAME_QP,
      g_param_spec_int ("max-bframe-qp", "Minimum B frame QP",
          "The minimum Quantization step size for B frames (dynamic control)",
          -G_MAXINT8, G_MAXINT8, DEFAULT_MAX_QP,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_MIN_QP,
      g_param_spec_int ("min-qp", "Minimum QP",
          "The minimum Quantization step size for all frames (dynamic control)",
          -G_MAXINT8, G_MAXINT8, DEFAULT_MIN_QP,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_MAX_QP,
      g_param_spec_int ("max-qp", "Minimum QP",
          "The minimum Quantization step size for all frames (dynamic control)",
          -G_MAXINT8, G_MAXINT8, DEFAULT_MAX_QP,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
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

  self->v4l2_fd = -1;
  self->auto_start = TRUE;
  gst_base_camera_src_set_mode (GST_BASE_CAMERA_SRC (self), MODE_VIDEO);
  /* Static controls */
  self->initial_bitrate = DEFAULT_INITIAL_BITRATE;
  self->slice_units = DEFAULT_SLICE_UNITS;
  self->slice_mode = DEFAULT_SLICE_MODE;
  self->iframe_period = DEFAULT_IFRAME_PERIOD;
  self->usage_type = DEFAULT_USAGE_TYPE;
  self->stream_format = DEFAULT_STREAM_FORMAT;
  self->entropy = DEFAULT_ENTROPY;
  self->enable_sei = DEFAULT_ENABLE_SEI;
  self->num_reorder_frames = DEFAULT_NUM_REORDER_FRAMES;
  self->preview_flipped = DEFAULT_PREVIEW_FLIPPED;

  /* Dynamic controls */
  self->rate_control = DEFAULT_RATE_CONTROL;
  self->fixed_framerate = DEFAULT_FIXED_FRAMERATE;
  self->level_idc = DEFAULT_LEVEL_IDC;
  self->peak_bitrate = DEFAULT_PEAK_BITRATE;
  self->average_bitrate = DEFAULT_AVERAGE_BITRATE;
  self->min_qp[QP_ALL_FRAMES] = DEFAULT_MIN_QP;
  self->max_qp[QP_ALL_FRAMES] = DEFAULT_MAX_QP;
  self->min_qp[QP_I_FRAME] = DEFAULT_MIN_QP;
  self->max_qp[QP_I_FRAME] = DEFAULT_MAX_QP;
  self->min_qp[QP_P_FRAME] = DEFAULT_MIN_QP;
  self->max_qp[QP_P_FRAME] = DEFAULT_MAX_QP;
  self->min_qp[QP_B_FRAME] = DEFAULT_MIN_QP;
  self->max_qp[QP_B_FRAME] = DEFAULT_MAX_QP;
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
      /* Static controls */
    case PROP_INITIAL_BITRATE:
      self->initial_bitrate = g_value_get_uint (value);
      break;
    case PROP_SLICE_UNITS:
      self->slice_units = g_value_get_uint (value);
      break;
    case PROP_SLICE_MODE:
      self->slice_mode = g_value_get_enum (value);
      break;
    case PROP_IFRAME_PERIOD:
      self->iframe_period = g_value_get_uint (value);
      break;
    case PROP_USAGE_TYPE:
      self->usage_type = g_value_get_enum (value);
      break;
    case PROP_STREAM_FORMAT:
      self->stream_format = g_value_get_enum (value);
      break;
    case PROP_ENTROPY:
      self->entropy = g_value_get_enum (value);
      break;
    case PROP_ENABLE_SEI:
      self->enable_sei = g_value_get_boolean (value);
      break;
    case PROP_NUM_REORDER_FRAMES:
      self->num_reorder_frames = g_value_get_uint (value);
      break;
    case PROP_PREVIEW_FLIPPED:
      self->preview_flipped = g_value_get_boolean (value);
      break;
      /* Dynamic controls */
    case PROP_RATE_CONTROL:
      self->rate_control = g_value_get_enum (value);
      break;
    case PROP_FIXED_FRAMERATE:
      self->fixed_framerate = g_value_get_boolean (value);
      break;
    case PROP_LEVEL_IDC:
      self->level_idc = g_value_get_uint (value);
      break;
    case PROP_PEAK_BITRATE:
      self->peak_bitrate = g_value_get_uint (value);
      break;
    case PROP_AVERAGE_BITRATE:
      self->average_bitrate = g_value_get_uint (value);
      break;
    case PROP_MIN_QP:
      self->min_qp[QP_ALL_FRAMES] = g_value_get_int (value);
      self->min_qp[QP_I_FRAME] = self->min_qp[QP_P_FRAME] =
          self->min_qp[QP_B_FRAME] = self->min_qp[QP_ALL_FRAMES];
      break;
    case PROP_MAX_QP:
      self->max_qp[QP_ALL_FRAMES] = g_value_get_int (value);
      self->max_qp[QP_I_FRAME] = self->max_qp[QP_P_FRAME] =
          self->max_qp[QP_B_FRAME] = self->max_qp[QP_ALL_FRAMES];
      break;
    case PROP_MIN_IFRAME_QP:
      self->min_qp[QP_I_FRAME] = g_value_get_int (value);
      break;
    case PROP_MAX_IFRAME_QP:
      self->max_qp[QP_I_FRAME] = g_value_get_int (value);
      break;
    case PROP_MIN_PFRAME_QP:
      self->min_qp[QP_P_FRAME] = g_value_get_int (value);
      break;
    case PROP_MAX_PFRAME_QP:
      self->max_qp[QP_P_FRAME] = g_value_get_int (value);
      break;
    case PROP_MIN_BFRAME_QP:
      self->min_qp[QP_B_FRAME] = g_value_get_int (value);
      break;
    case PROP_MAX_BFRAME_QP:
      self->max_qp[QP_B_FRAME] = g_value_get_int (value);
      break;
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
  uvcx_video_config_probe_commit_t probe;

  switch (prop_id) {
    case PROP_INITIAL_BITRATE:
    case PROP_SLICE_UNITS:
    case PROP_SLICE_MODE:
    case PROP_IFRAME_PERIOD:
    case PROP_USAGE_TYPE:
    case PROP_STREAM_FORMAT:
    case PROP_ENTROPY:
    case PROP_ENABLE_SEI:
    case PROP_NUM_REORDER_FRAMES:
    case PROP_PREVIEW_FLIPPED:
      fill_probe_commit_t (self, &probe, 0, 0, 0, 0);
      if (self->v4l2_fd != -1) {
        xu_query (self, UVCX_VIDEO_CONFIG_PROBE, UVC_QUERY_GET_CUR,
            (guchar *) & probe);
      }
      break;
    default:
      break;
  }


  switch (prop_id) {
      /* Static controls */
    case PROP_INITIAL_BITRATE:
      g_value_set_uint (value, probe.dwBitRate);
      break;
    case PROP_SLICE_UNITS:
      g_value_set_uint (value, probe.wSliceUnits);
      break;
    case PROP_SLICE_MODE:
      g_value_set_enum (value, probe.wSliceMode);
      break;
    case PROP_IFRAME_PERIOD:
      g_value_set_uint (value, probe.wIFramePeriod);
      break;
    case PROP_USAGE_TYPE:
      g_value_set_enum (value, probe.bUsageType);
      break;
    case PROP_STREAM_FORMAT:
      g_value_set_enum (value, probe.bStreamFormat);
      break;
    case PROP_ENTROPY:
      g_value_set_enum (value, probe.bEntropyCABAC);
      break;
    case PROP_ENABLE_SEI:
      g_value_set_boolean (value,
          (probe.bTimestamp == UVC_H264_TIMESTAMP_SEI_ENABLE));
      break;
    case PROP_NUM_REORDER_FRAMES:
      g_value_set_uint (value, probe.bNumOfReorderFrames);
      break;
    case PROP_PREVIEW_FLIPPED:
      g_value_set_boolean (value,
          (probe.bPreviewFlipped == UVC_H264_PREFLIPPED_HORIZONTAL));
      break;
      /* Dynamic controls */
    case PROP_RATE_CONTROL:
      g_value_set_enum (value, self->rate_control);
      break;
    case PROP_FIXED_FRAMERATE:
      g_value_set_boolean (value, self->fixed_framerate);
      break;
    case PROP_MAX_MBPS:
      g_value_set_uint (value, 0);
      break;
    case PROP_LEVEL_IDC:
      g_value_set_uint (value, self->level_idc);
      break;
    case PROP_PEAK_BITRATE:
      g_value_set_uint (value, self->peak_bitrate);
      break;
    case PROP_AVERAGE_BITRATE:
      g_value_set_uint (value, self->average_bitrate);
      break;
    case PROP_MIN_QP:
      g_value_set_int (value, self->min_qp[QP_ALL_FRAMES]);
      break;
    case PROP_MAX_QP:
      g_value_set_int (value, self->max_qp[QP_ALL_FRAMES]);
      break;
    case PROP_MIN_IFRAME_QP:
      g_value_set_int (value, self->min_qp[QP_I_FRAME]);
      break;
    case PROP_MAX_IFRAME_QP:
      g_value_set_int (value, self->max_qp[QP_I_FRAME]);
      break;
    case PROP_MIN_PFRAME_QP:
      g_value_set_int (value, self->min_qp[QP_P_FRAME]);
      break;
    case PROP_MAX_PFRAME_QP:
      g_value_set_int (value, self->max_qp[QP_P_FRAME]);
      break;
    case PROP_MIN_BFRAME_QP:
      g_value_set_int (value, self->min_qp[QP_B_FRAME]);
      break;
    case PROP_MAX_BFRAME_QP:
      g_value_set_int (value, self->max_qp[QP_B_FRAME]);
      break;
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
xu_query (GstUvcH264Src * self, guint selector, guint query, guchar * data)
{
  struct uvc_xu_control_query xu;
  __u16 len;

  xu.unit = 12;                 /* TODO: find the right unit */
  xu.selector = selector;

  xu.query = UVC_QUERY_GET_LEN;
  xu.size = sizeof (len);
  xu.data = (unsigned char *) &len;
  if (-1 == ioctl (self->v4l2_fd, UVCIOC_CTRL_QUERY, &xu)) {
    GST_WARNING_OBJECT (self, "PROBE GET_LEN error");
    return FALSE;
  }

  xu.query = query;
  xu.size = len;
  xu.data = data;
  if (-1 == ioctl (self->v4l2_fd, UVCIOC_CTRL_QUERY, &xu)) {
    return FALSE;
  }

  return TRUE;
}

static void
fill_probe_commit_t (GstUvcH264Src * self,
    uvcx_video_config_probe_commit_t * probe, guint32 frame_interval,
    guint32 width, guint32 height, guint32 profile)
{
  probe->dwFrameInterval = frame_interval;
  probe->dwBitRate = self->initial_bitrate;
  probe->wWidth = width;
  probe->wHeight = height;
  probe->wSliceUnits = self->slice_units;
  probe->wSliceMode = self->slice_mode;
  probe->wProfile = profile;
  probe->wIFramePeriod = self->iframe_period;
  probe->bUsageType = self->usage_type;
  probe->bRateControlMode = self->rate_control;
  if (self->fixed_framerate)
    probe->bRateControlMode |= UVC_H264_RATECONTROL_FIXED_FRM_FLG;
  probe->bStreamFormat = self->stream_format;
  probe->bEntropyCABAC = self->entropy;
  probe->bTimestamp = self->enable_sei ?
      UVC_H264_TIMESTAMP_SEI_ENABLE : UVC_H264_TIMESTAMP_SEI_DISABLE;
  probe->bNumOfReorderFrames = self->num_reorder_frames;
  probe->bPreviewFlipped = self->preview_flipped ?
      UVC_H264_PREFLIPPED_HORIZONTAL : UVC_H264_PREFLIPPED_DISABLE;
}

static void
print_probe_commit_t (GstUvcH264Src * self,
    uvcx_video_config_probe_commit_t * probe)
{
  GST_DEBUG_OBJECT (self, "  Frame interval : %d *100ns",
      probe->dwFrameInterval);
  GST_DEBUG_OBJECT (self, "  Bit rate : %d", probe->dwBitRate);
  GST_DEBUG_OBJECT (self, "  Hints : %X", probe->bmHints);
  GST_DEBUG_OBJECT (self, "  Configuration index : %d",
      probe->wConfigurationIndex);
  GST_DEBUG_OBJECT (self, "  Width : %d", probe->wWidth);
  GST_DEBUG_OBJECT (self, "  Height : %d", probe->wHeight);
  GST_DEBUG_OBJECT (self, "  Slice units : %X", probe->wSliceUnits);
  GST_DEBUG_OBJECT (self, "  Slice mode : %X", probe->wSliceMode);
  GST_DEBUG_OBJECT (self, "  Profile : %X", probe->wProfile);
  GST_DEBUG_OBJECT (self, "  IFrame Period : %d ms", probe->wIFramePeriod);
  GST_DEBUG_OBJECT (self, "  Estimated video delay : %d ms",
      probe->wEstimatedVideoDelay);
  GST_DEBUG_OBJECT (self, "  Estimated max config delay : %d ms",
      probe->wEstimatedMaxConfigDelay);
  GST_DEBUG_OBJECT (self, "  Usage type : %X", probe->bUsageType);
  GST_DEBUG_OBJECT (self, "  Rate control mode : %X", probe->bRateControlMode);
  GST_DEBUG_OBJECT (self, "  Temporal scale mode : %X",
      probe->bTemporalScaleMode);
  GST_DEBUG_OBJECT (self, "  Spatial scale mode : %X",
      probe->bSpatialScaleMode);
  GST_DEBUG_OBJECT (self, "  SNR scale mode : %X", probe->bSNRScaleMode);
  GST_DEBUG_OBJECT (self, "  Stream mux option : %X", probe->bStreamMuxOption);
  GST_DEBUG_OBJECT (self, "  Stream Format : %X", probe->bStreamFormat);
  GST_DEBUG_OBJECT (self, "  Entropy CABAC : %X", probe->bEntropyCABAC);
  GST_DEBUG_OBJECT (self, "  Timestamp : %X", probe->bTimestamp);
  GST_DEBUG_OBJECT (self, "  Num of reorder frames : %d",
      probe->bNumOfReorderFrames);
  GST_DEBUG_OBJECT (self, "  Preview flipped : %X", probe->bPreviewFlipped);
  GST_DEBUG_OBJECT (self, "  View : %d", probe->bView);
  GST_DEBUG_OBJECT (self, "  Stream ID : %X", probe->bStreamID);
  GST_DEBUG_OBJECT (self, "  Spatial layer ratio : %f",
      ((probe->bSpatialLayerRatio & 0xF0) >> 4) +
      ((float) (probe->bSpatialLayerRatio & 0x0F)) / 16);
  GST_DEBUG_OBJECT (self, "  Leaky bucket size : %d ms",
      probe->wLeakyBucketSize);
}

static void
configure_h264 (GstUvcH264Src * self, gint fd)
{
  uvcx_video_config_probe_commit_t probe;

  if (!xu_query (self, UVCX_VIDEO_CONFIG_PROBE, UVC_QUERY_GET_MAX,
          (guchar *) & probe)) {
    GST_WARNING_OBJECT (self, "PROBE GET_MAX error");
    return;
  }
  GST_DEBUG_OBJECT (self, "PROBE GET_MAX : ");
  print_probe_commit_t (self, &probe);

  if (!xu_query (self, UVCX_VIDEO_CONFIG_PROBE, UVC_QUERY_GET_CUR,
          (guchar *) & probe)) {
    GST_WARNING_OBJECT (self, "PROBE GET_CUR error");
    return;
  }
  GST_DEBUG_OBJECT (self, "PROBE GET_CUR : ");
  print_probe_commit_t (self, &probe);

  if (!xu_query (self, UVCX_VIDEO_CONFIG_PROBE, UVC_QUERY_GET_DEF,
          (guchar *) & probe)) {
    GST_WARNING_OBJECT (self, "PROBE GET_DEF error");
    return;
  }
  GST_DEBUG_OBJECT (self, "PROBE GET_DEF : ");
  print_probe_commit_t (self, &probe);

  fill_probe_commit_t (self, &probe, 333333, 1920, 1080, 0x4240);
  probe.bStreamMuxOption = 3;

  if (!xu_query (self, UVCX_VIDEO_CONFIG_PROBE, UVC_QUERY_SET_CUR,
          (guchar *) & probe)) {
    GST_WARNING_OBJECT (self, "PROBE SET_CUR error");
    return;
  }

  if (!xu_query (self, UVCX_VIDEO_CONFIG_PROBE, UVC_QUERY_GET_CUR,
          (guchar *) & probe)) {
    GST_WARNING_OBJECT (self, "PROBE GET_CUR error");
    return;
  }
  GST_DEBUG_OBJECT (self, "PROBE SET_CUR : ");
  print_probe_commit_t (self, &probe);

  if (!xu_query (self, UVCX_VIDEO_CONFIG_COMMIT, UVC_QUERY_SET_CUR,
          (guchar *) & probe)) {
    GST_WARNING_OBJECT (self, "COMMIT SET_CUR error");
    return;
  }
}

static void
v4l2src_pre_set_format (GstElement * v4l2src, gint fd, guint fourcc,
    guint width, guint height, gpointer user_data)
{
  GstUvcH264Src *self = GST_UVC_H264_SRC (user_data);

  self->v4l2_fd = fd;
  if (fourcc == GST_MAKE_FOURCC ('M', 'J', 'P', 'G')) {
    configure_h264 (self, fd);
  }
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
    g_signal_connect (self->v4l2_src, "prepare-format",
        (GCallback) v4l2src_pre_set_format, self);
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
