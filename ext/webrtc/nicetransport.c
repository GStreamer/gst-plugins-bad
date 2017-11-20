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

#include "nicetransport.h"
#include "icestream.h"

#define GST_CAT_DEFAULT gst_webrtc_nice_transport_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

#define gst_webrtc_nice_transport_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstWebRTCNiceTransport, gst_webrtc_nice_transport,
    GST_TYPE_WEBRTC_ICE_TRANSPORT,
    GST_DEBUG_CATEGORY_INIT (gst_webrtc_nice_transport_debug,
        "webrtcnicetransport", 0, "webrtcnicetransport");
    );

enum
{
  SIGNAL_0,
  LAST_SIGNAL,
};

enum
{
  PROP_0,
  PROP_STREAM,
};

//static guint gst_webrtc_nice_transport_signals[LAST_SIGNAL] = { 0 };

struct _GstWebRTCNiceTransportPrivate
{
  gboolean running;
};

static NiceComponentType
_gst_component_to_nice (GstWebRTCICEComponent component)
{
  switch (component) {
    case GST_WEBRTC_ICE_COMPONENT_RTP:
      return NICE_COMPONENT_TYPE_RTP;
    case GST_WEBRTC_ICE_COMPONENT_RTCP:
      return NICE_COMPONENT_TYPE_RTCP;
    default:
      g_assert_not_reached ();
      return 0;
  }
}

static GstWebRTCICEComponent
_nice_component_to_gst (NiceComponentType component)
{
  switch (component) {
    case NICE_COMPONENT_TYPE_RTP:
      return GST_WEBRTC_ICE_COMPONENT_RTP;
    case NICE_COMPONENT_TYPE_RTCP:
      return GST_WEBRTC_ICE_COMPONENT_RTCP;
    default:
      g_assert_not_reached ();
      return 0;
  }
}

static GstWebRTCICEConnectionState
_nice_component_state_to_gst (NiceComponentState state)
{
  switch (state) {
    case NICE_COMPONENT_STATE_DISCONNECTED:
      return GST_WEBRTC_ICE_CONNECTION_STATE_DISCONNECTED;
    case NICE_COMPONENT_STATE_GATHERING:
      return GST_WEBRTC_ICE_CONNECTION_STATE_NEW;
    case NICE_COMPONENT_STATE_CONNECTING:
      return GST_WEBRTC_ICE_CONNECTION_STATE_CHECKING;
    case NICE_COMPONENT_STATE_CONNECTED:
      return GST_WEBRTC_ICE_CONNECTION_STATE_CONNECTED;
    case NICE_COMPONENT_STATE_READY:
      return GST_WEBRTC_ICE_CONNECTION_STATE_COMPLETED;
    case NICE_COMPONENT_STATE_FAILED:
      return GST_WEBRTC_ICE_CONNECTION_STATE_FAILED;
    default:
      g_assert_not_reached ();
      return 0;
  }
}

static void
gst_webrtc_nice_transport_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstWebRTCNiceTransport *nice = GST_WEBRTC_NICE_TRANSPORT (object);

  switch (prop_id) {
    case PROP_STREAM:
      if (nice->stream)
        gst_object_unref (nice->stream);
      nice->stream = g_value_dup_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_webrtc_nice_transport_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstWebRTCNiceTransport *nice = GST_WEBRTC_NICE_TRANSPORT (object);

  switch (prop_id) {
    case PROP_STREAM:
      g_value_set_object (value, nice->stream);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_webrtc_nice_transport_finalize (GObject * object)
{
  GstWebRTCNiceTransport *nice = GST_WEBRTC_NICE_TRANSPORT (object);

  g_object_unref (nice->stream);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
_on_new_selected_pair (NiceAgent * agent, guint stream_id,
    NiceComponentType component, NiceCandidate * lcandidate,
    NiceCandidate * rcandidate, GstWebRTCNiceTransport * nice)
{
  GstWebRTCICETransport *ice = GST_WEBRTC_ICE_TRANSPORT (nice);
  GstWebRTCICEComponent comp = _nice_component_to_gst (component);
  guint our_stream_id;

  g_object_get (nice->stream, "stream-id", &our_stream_id, NULL);

  if (stream_id != our_stream_id)
    return;
  if (comp != ice->component)
    return;

  gst_webrtc_ice_transport_selected_pair_change (ice);
}

static void
_on_component_state_changed (NiceAgent * agent, guint stream_id,
    NiceComponentType component, NiceComponentState state,
    GstWebRTCNiceTransport * nice)
{
  GstWebRTCICETransport *ice = GST_WEBRTC_ICE_TRANSPORT (nice);
  GstWebRTCICEComponent comp = _nice_component_to_gst (component);
  guint our_stream_id;

  g_object_get (nice->stream, "stream-id", &our_stream_id, NULL);

  if (stream_id != our_stream_id)
    return;
  if (comp != ice->component)
    return;

  GST_DEBUG_OBJECT (ice, "%u %u %s", stream_id, component,
      nice_component_state_to_string (state));

  gst_webrtc_ice_transport_connection_state_change (ice,
      _nice_component_state_to_gst (state));
}

static void
gst_webrtc_nice_transport_constructed (GObject * object)
{
  GstWebRTCNiceTransport *nice = GST_WEBRTC_NICE_TRANSPORT (object);
  GstWebRTCICETransport *ice = GST_WEBRTC_ICE_TRANSPORT (object);
  NiceComponentType component = _gst_component_to_nice (ice->component);
  gboolean controlling_mode;
  guint our_stream_id;
  NiceAgent *agent;

  g_object_get (nice->stream, "stream-id", &our_stream_id, NULL);
  g_object_get (nice->stream->ice, "agent", &agent, NULL);

  g_object_get (agent, "controlling-mode", &controlling_mode, NULL);
  ice->role =
      controlling_mode ? GST_WEBRTC_ICE_ROLE_CONTROLLING :
      GST_WEBRTC_ICE_ROLE_CONTROLLED;

  g_signal_connect (agent, "component-state-changed",
      G_CALLBACK (_on_component_state_changed), nice);
  g_signal_connect (agent, "new-selected-pair-full",
      G_CALLBACK (_on_new_selected_pair), nice);

  ice->src = gst_element_factory_make ("nicesrc", NULL);
  if (ice->src) {
    g_object_set (ice->src, "agent", agent, "stream", our_stream_id,
        "component", component, NULL);
  }
  ice->sink = gst_element_factory_make ("nicesink", NULL);
  if (ice->sink) {
    g_object_set (ice->sink, "agent", agent, "stream", our_stream_id,
        "component", component, "async", FALSE, "enable-last-sample", FALSE,
        NULL);
    if (ice->component == GST_WEBRTC_ICE_COMPONENT_RTCP)
      g_object_set (ice->sink, "sync", FALSE, NULL);
  }

  g_object_unref (agent);

  G_OBJECT_CLASS (parent_class)->constructed (object);
}

static void
gst_webrtc_nice_transport_class_init (GstWebRTCNiceTransportClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;

  g_type_class_add_private (klass, sizeof (GstWebRTCNiceTransportPrivate));

  gobject_class->constructed = gst_webrtc_nice_transport_constructed;
  gobject_class->get_property = gst_webrtc_nice_transport_get_property;
  gobject_class->set_property = gst_webrtc_nice_transport_set_property;
  gobject_class->finalize = gst_webrtc_nice_transport_finalize;

  g_object_class_install_property (gobject_class,
      PROP_STREAM,
      g_param_spec_object ("stream",
          "WebRTC ICE Stream", "ICE stream associated with this transport",
          GST_TYPE_WEBRTC_ICE_STREAM,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
}

static void
gst_webrtc_nice_transport_init (GstWebRTCNiceTransport * nice)
{
  nice->priv =
      G_TYPE_INSTANCE_GET_PRIVATE ((nice), GST_TYPE_WEBRTC_NICE_TRANSPORT,
      GstWebRTCNiceTransportPrivate);
}

GstWebRTCNiceTransport *
gst_webrtc_nice_transport_new (GstWebRTCICEStream * stream,
    GstWebRTCICEComponent component)
{
  return g_object_new (GST_TYPE_WEBRTC_NICE_TRANSPORT, "stream", stream,
      "component", component, NULL);
}
