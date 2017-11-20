/* GStreamer
 * Copyright (C) 2017 Matthew Waters <matthew@centricular.com>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "rtptransceiver.h"

#define GST_CAT_DEFAULT gst_webrtc_rtp_transceiver_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

#define gst_webrtc_rtp_transceiver_parent_class parent_class
G_DEFINE_ABSTRACT_TYPE_WITH_CODE (GstWebRTCRTPTransceiver,
    gst_webrtc_rtp_transceiver, GST_TYPE_OBJECT,
    GST_DEBUG_CATEGORY_INIT (gst_webrtc_rtp_transceiver_debug,
        "webrtctransceiver", 0, "webrtctransceiver");
    );

enum
{
  SIGNAL_0,
  LAST_SIGNAL,
};

enum
{
  PROP_0,
  PROP_MID,
  PROP_SENDER,
  PROP_RECEIVER,
  PROP_STOPPED,                 // FIXME
  PROP_DIRECTION,               // FIXME
  PROP_MLINE,
};

//static guint gst_webrtc_rtp_transceiver_signals[LAST_SIGNAL] = { 0 };

static void
gst_webrtc_rtp_transceiver_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstWebRTCRTPTransceiver *webrtc = GST_WEBRTC_RTP_TRANSCEIVER (object);

  switch (prop_id) {
    case PROP_SENDER:
      webrtc->sender = g_value_dup_object (value);
      break;
    case PROP_RECEIVER:
      webrtc->receiver = g_value_dup_object (value);
      break;
    case PROP_MLINE:
      webrtc->mline = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_webrtc_rtp_transceiver_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstWebRTCRTPTransceiver *webrtc = GST_WEBRTC_RTP_TRANSCEIVER (object);

  switch (prop_id) {
    case PROP_SENDER:
      g_value_set_object (value, webrtc->sender);
      break;
    case PROP_RECEIVER:
      g_value_set_object (value, webrtc->receiver);
      break;
    case PROP_MLINE:
      g_value_set_uint (value, webrtc->mline);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_webrtc_rtp_transceiver_constructed (GObject * object)
{
  GstWebRTCRTPTransceiver *webrtc = GST_WEBRTC_RTP_TRANSCEIVER (object);

  gst_object_set_parent (GST_OBJECT (webrtc->sender), GST_OBJECT (webrtc));
  gst_object_set_parent (GST_OBJECT (webrtc->receiver), GST_OBJECT (webrtc));

  G_OBJECT_CLASS (parent_class)->constructed (object);
}

static void
gst_webrtc_rtp_transceiver_dispose (GObject * object)
{
  GstWebRTCRTPTransceiver *webrtc = GST_WEBRTC_RTP_TRANSCEIVER (object);

  if (webrtc->sender) {
    GST_OBJECT_PARENT (webrtc->sender) = NULL;
    gst_object_unref (webrtc->sender);
  }
  webrtc->sender = NULL;
  if (webrtc->receiver) {
    GST_OBJECT_PARENT (webrtc->receiver) = NULL;
    gst_object_unref (webrtc->receiver);
  }
  webrtc->receiver = NULL;

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_webrtc_rtp_transceiver_finalize (GObject * object)
{
  GstWebRTCRTPTransceiver *webrtc = GST_WEBRTC_RTP_TRANSCEIVER (object);

  g_free (webrtc->mid);
  if (webrtc->codec_preferences)
    gst_caps_unref (webrtc->codec_preferences);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_webrtc_rtp_transceiver_class_init (GstWebRTCRTPTransceiverClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;

  gobject_class->get_property = gst_webrtc_rtp_transceiver_get_property;
  gobject_class->set_property = gst_webrtc_rtp_transceiver_set_property;
  gobject_class->constructed = gst_webrtc_rtp_transceiver_constructed;
  gobject_class->dispose = gst_webrtc_rtp_transceiver_dispose;
  gobject_class->finalize = gst_webrtc_rtp_transceiver_finalize;

  g_object_class_install_property (gobject_class,
      PROP_SENDER,
      g_param_spec_object ("sender", "Sender",
          "The RTP sender for this transceiver",
          GST_TYPE_WEBRTC_RTP_SENDER,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
      PROP_RECEIVER,
      g_param_spec_object ("receiver", "Receiver",
          "The RTP receiver for this transceiver",
          GST_TYPE_WEBRTC_RTP_RECEIVER,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
      PROP_MLINE,
      g_param_spec_uint ("mlineindex", "Media Line Index",
          "Index in the SDP of the Media",
          0, G_MAXUINT, 0,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
}

static void
gst_webrtc_rtp_transceiver_init (GstWebRTCRTPTransceiver * webrtc)
{
}
