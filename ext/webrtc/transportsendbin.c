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

#include "transportsendbin.h"
#include "utils.h"

/*
 *           ,------------------------transport_send_%u-------------------------,
 *           ;                          ,-----dtlssrtpenc---,                   ;
 *  rtp_sink o--------------------------o rtp_sink_0        ;  ,---nicesink---, ;
 *           ;                          ;               src o--o sink         ; ;
 *           ;   ,--outputselector--, ,-o rtcp_sink_0       ;  '--------------' ;
 *           ;   ;            src_0 o-' '-------------------'                   ;
 * rtcp_sink ;---o sink             ;   ,----dtlssrtpenc----,  ,---nicesink---, ;
 *           ;   ;            src_1 o---o rtcp_sink_0   src o--o sink         ; ;
 *           ;   '------------------'   '-------------------'  '--------------' ;
 *           '------------------------------------------------------------------'
 *
 * outputselecter is used to switch between rtcp-mux and no rtcp-mux
 *
 * FIXME: Do we need a valve drop=TRUE for the no RTCP case?
 */

#define GST_CAT_DEFAULT gst_webrtc_transport_send_bin_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

#define transport_send_bin_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (TransportSendBin, transport_send_bin, GST_TYPE_BIN,
    GST_DEBUG_CATEGORY_INIT (gst_webrtc_transport_send_bin_debug,
        "webrtctransportsendbin", 0, "webrtctransportsendbin"););

static GstStaticPadTemplate rtp_sink_template =
GST_STATIC_PAD_TEMPLATE ("rtp_sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp"));

static GstStaticPadTemplate rtcp_sink_template =
GST_STATIC_PAD_TEMPLATE ("rtcp_sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp"));

enum
{
  PROP_0,
  PROP_STREAM,
  PROP_RTCP_MUX,
};

static void
_set_rtcp_mux (TransportSendBin * send, gboolean rtcp_mux)
{
  GstPad *active_pad;

  if (rtcp_mux)
    active_pad = gst_element_get_static_pad (send->outputselector, "src_0");
  else
    active_pad = gst_element_get_static_pad (send->outputselector, "src_1");
  send->rtcp_mux = rtcp_mux;
  GST_OBJECT_UNLOCK (send);

  g_object_set (send->outputselector, "active-pad", active_pad, NULL);

  gst_object_unref (active_pad);
  GST_OBJECT_LOCK (send);
}

static void
transport_send_bin_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  TransportSendBin *send = TRANSPORT_SEND_BIN (object);

  GST_OBJECT_LOCK (send);
  switch (prop_id) {
    case PROP_STREAM:
      /* XXX: weak-ref this? */
      send->stream = TRANSPORT_STREAM (g_value_get_object (value));
      break;
    case PROP_RTCP_MUX:
      _set_rtcp_mux (send, g_value_get_boolean (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (send);
}

static void
transport_send_bin_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  TransportSendBin *send = TRANSPORT_SEND_BIN (object);

  GST_OBJECT_LOCK (send);
  switch (prop_id) {
    case PROP_STREAM:
      g_value_set_object (value, send->stream);
      break;
    case PROP_RTCP_MUX:
      g_value_set_boolean (value, send->rtcp_mux);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (send);
}

static GstPadProbeReturn
pad_block (GstPad * pad, GstPadProbeInfo * info, gpointer unused)
{
  GST_LOG_OBJECT (pad, "blocking pad with data %" GST_PTR_FORMAT, info->data);

  return GST_PAD_PROBE_OK;
}

static GstStateChangeReturn
transport_send_bin_change_state (GstElement * element,
    GstStateChange transition)
{
  TransportSendBin *send = TRANSPORT_SEND_BIN (element);
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  GST_DEBUG ("changing state: %s => %s",
      gst_element_state_get_name (GST_STATE_TRANSITION_CURRENT (transition)),
      gst_element_state_get_name (GST_STATE_TRANSITION_NEXT (transition)));

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:{
      GstPad *pad;

      /* XXX: don't change state until the client-ness has been chosen
       * arguably the element should be able to deal with this itself */
      gst_element_set_locked_state (send->stream->transport->dtlssrtpenc, TRUE);

      /* unblock the encoder once the key is set, this should also be automatic */
      pad =
          gst_element_get_static_pad (send->stream->transport->dtlssrtpenc,
          "rtp_sink_0");
      send->rtp_block =
          _create_pad_block (send->stream->transport->dtlssrtpenc, pad, 0, NULL,
          NULL);
      send->rtp_block->block_id =
          gst_pad_add_probe (pad,
          GST_PAD_PROBE_TYPE_BLOCK | GST_PAD_PROBE_TYPE_BUFFER |
          GST_PAD_PROBE_TYPE_BUFFER_LIST, (GstPadProbeCallback) pad_block, NULL,
          NULL);
      gst_object_unref (pad);

      /* unblock the encoder once the key is set, this should also be automatic */
      pad =
          gst_element_get_static_pad (send->stream->transport->dtlssrtpenc,
          "rtcp_sink_0");
      send->rtcp_mux_block =
          _create_pad_block (send->stream->transport->dtlssrtpenc, pad, 0, NULL,
          NULL);
      send->rtcp_mux_block->block_id =
          gst_pad_add_probe (pad,
          GST_PAD_PROBE_TYPE_BLOCK | GST_PAD_PROBE_TYPE_BUFFER |
          GST_PAD_PROBE_TYPE_BUFFER_LIST, (GstPadProbeCallback) pad_block, NULL,
          NULL);
      gst_object_unref (pad);

      gst_element_set_locked_state (send->stream->rtcp_transport->dtlssrtpenc,
          TRUE);

      /* unblock the encoder once the key is set, this should also be automatic */
      pad =
          gst_element_get_static_pad (send->stream->rtcp_transport->dtlssrtpenc,
          "rtcp_sink_0");
      send->rtcp_block =
          _create_pad_block (send->stream->rtcp_transport->dtlssrtpenc, pad, 0,
          NULL, NULL);
      send->rtcp_block->block_id =
          gst_pad_add_probe (pad,
          GST_PAD_PROBE_TYPE_BLOCK | GST_PAD_PROBE_TYPE_BUFFER |
          GST_PAD_PROBE_TYPE_BUFFER_LIST, (GstPadProbeCallback) pad_block, NULL,
          NULL);
      gst_object_unref (pad);
      break;
    }
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      if (send->rtp_block)
        _free_pad_block (send->rtp_block);
      send->rtp_block = NULL;
      if (send->rtcp_mux_block)
        _free_pad_block (send->rtcp_mux_block);
      send->rtcp_mux_block = NULL;
      gst_element_set_locked_state (send->stream->transport->dtlssrtpenc,
          FALSE);

      if (send->rtcp_block)
        _free_pad_block (send->rtcp_block);
      send->rtcp_block = NULL;
      gst_element_set_locked_state (send->stream->rtcp_transport->dtlssrtpenc,
          FALSE);
      break;
    default:
      break;
  }

  return ret;
}

static void
_on_dtls_enc_key_set (GstElement * element, TransportSendBin * send)
{
  if (element == send->stream->transport->dtlssrtpenc) {
    _free_pad_block (send->rtp_block);
    send->rtp_block = NULL;
    _free_pad_block (send->rtcp_mux_block);
    send->rtcp_mux_block = NULL;
  } else if (element == send->stream->rtcp_transport->dtlssrtpenc) {
    _free_pad_block (send->rtcp_block);
    send->rtcp_block = NULL;
  }
}

static void
transport_send_bin_constructed (GObject * object)
{
  TransportSendBin *send = TRANSPORT_SEND_BIN (object);
  GstPadTemplate *templ;
  GstWebRTCDTLSTransport *transport;
  GstPad *ghost, *pad;

  g_return_if_fail (send->stream);

  g_object_bind_property (send, "rtcp-mux", send->stream, "rtcp-mux",
      G_BINDING_BIDIRECTIONAL);

  transport = send->stream->transport;

  templ = _find_pad_template (transport->dtlssrtpenc,
      GST_PAD_SINK, GST_PAD_REQUEST, "rtp_sink_%d");
  pad = gst_element_request_pad (transport->dtlssrtpenc, templ, "rtp_sink_0",
      NULL);

  /* unblock the encoder once the key is set */
  g_signal_connect (transport->dtlssrtpenc, "on-key-set",
      G_CALLBACK (_on_dtls_enc_key_set), send);
  gst_bin_add (GST_BIN (send), GST_ELEMENT (transport->dtlssrtpenc));

  gst_bin_add (GST_BIN (send), GST_ELEMENT (transport->transport->sink));

  if (!gst_element_link_pads (GST_ELEMENT (transport->dtlssrtpenc), "src",
          GST_ELEMENT (transport->transport->sink), "sink"))
    g_warn_if_reached ();

  send->outputselector = gst_element_factory_make ("output-selector", NULL);
  gst_bin_add (GST_BIN (send), send->outputselector);

  if (!gst_element_link_pads (GST_ELEMENT (send->outputselector), "src_0",
          GST_ELEMENT (transport->dtlssrtpenc), "rtcp_sink_0"))
    g_warn_if_reached ();

  ghost = gst_ghost_pad_new ("rtp_sink", pad);
  gst_element_add_pad (GST_ELEMENT (send), ghost);
  gst_object_unref (pad);

  transport = send->stream->rtcp_transport;

  templ = _find_pad_template (transport->dtlssrtpenc,
      GST_PAD_SINK, GST_PAD_REQUEST, "rtcp_sink_%d");

  gst_bin_add (GST_BIN (send), GST_ELEMENT (transport->dtlssrtpenc));
  gst_bin_add (GST_BIN (send), GST_ELEMENT (transport->transport->sink));

  if (!gst_element_link_pads (GST_ELEMENT (transport->dtlssrtpenc), "src",
          GST_ELEMENT (transport->transport->sink), "sink"))
    g_warn_if_reached ();

  if (!gst_element_link_pads (GST_ELEMENT (send->outputselector), "src_1",
          GST_ELEMENT (transport->dtlssrtpenc), "rtcp_sink_0"))
    g_warn_if_reached ();

  pad = gst_element_get_static_pad (send->outputselector, "sink");
  g_signal_connect (transport->dtlssrtpenc, "on-key-set",
      G_CALLBACK (_on_dtls_enc_key_set), send);

  ghost = gst_ghost_pad_new ("rtcp_sink", pad);
  gst_element_add_pad (GST_ELEMENT (send), ghost);
  gst_object_unref (pad);
}

static void
transport_send_bin_class_init (TransportSendBinClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *element_class = (GstElementClass *) klass;

  element_class->change_state = transport_send_bin_change_state;

  gst_element_class_add_static_pad_template (element_class, &rtp_sink_template);
  gst_element_class_add_static_pad_template (element_class,
      &rtcp_sink_template);

  gst_element_class_set_metadata (element_class, "WebRTC Transport Send Bin",
      "Filter/Network/WebRTC", "A bin for webrtc connections",
      "Matthew Waters <matthew@centricular.com>");

  gobject_class->constructed = transport_send_bin_constructed;
  gobject_class->get_property = transport_send_bin_get_property;
  gobject_class->set_property = transport_send_bin_set_property;

  g_object_class_install_property (gobject_class,
      PROP_STREAM,
      g_param_spec_object ("stream", "Stream",
          "The TransportStream for this sending bin",
          transport_stream_get_type (),
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
      PROP_RTCP_MUX,
      g_param_spec_boolean ("rtcp-mux", "RTCP Mux",
          "Whether RTCP packets are muxed with RTP packets",
          FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void
transport_send_bin_init (TransportSendBin * send)
{
}
