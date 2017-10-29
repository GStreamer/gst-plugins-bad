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

#include "gstwebrtcbin.h"
#include "transportstream.h"
#include "transportreceivebin.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RANDOM_SESSION_ID \
    ((((((guint64) g_random_int()) << 32) | \
       (guint64) g_random_int ())) & \
    G_GUINT64_CONSTANT (0x7fffffffffffffff))

#define IS_EMPTY_SDP_ATTRIBUTE(val) (val == NULL || g_strcmp0(val, "") == 0)

#define PC_GET_LOCK(w) (&w->priv->pc_lock)
#define PC_LOCK(w) (g_mutex_lock (PC_GET_LOCK(w)))
#define PC_UNLOCK(w) (g_mutex_unlock (PC_GET_LOCK(w)))

#define PC_GET_COND(w) (&w->priv->pc_cond)
#define PC_COND_WAIT(w) (g_cond_wait(PC_GET_COND(w), PC_GET_LOCK(w)))
#define PC_COND_BROADCAST(w) (g_cond_broadcast(PC_GET_COND(w)))
#define PC_COND_SIGNAL(w) (g_cond_signal(PC_GET_COND(w)))

/*
 * This webrtcbin implements the majority of the W3's peerconnection API and
 * implementation guide where possible. Generating offers, answers and setting
 * local and remote SDP's are all supported.  To start with, only the media
 * interface has been implemented (no datachannel yet).
 *
 * Each input/output pad is equivalent to a Track in W3 parlance which are
 * added/removed from the bin.  The number of requested sink pads is the number
 * of streams that will be sent to the receiver and will be associated with a
 * GstWebRTCRTPTransceiver (very similar to W3 RTPTransceiver's).
 *
 * On the receiving side, RTPTransceiver's are created in response to setting
 * a remote description.  Output pads for the receiving streams in the set
 * description are also created.
 */

/*
 * TODO:
 * assert sending payload type matches the stream
 * reconfiguration (of anything)
 * LS groups
 * bundling
 * setting custom DTLS certificates
 * data channel
 *
 * seperate session id's from mlineindex properly
 * how to deal with replacing a input/output track/stream
 */

#define GST_CAT_DEFAULT gst_webrtc_bin_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

GQuark
gst_webrtc_bin_error_quark (void)
{
  return g_quark_from_static_string ("gst-webrtc-bin-error-quark");
}

static gchar *
_enum_value_to_string (GType type, guint value)
{
  GEnumClass *enum_class;
  GEnumValue *enum_value;
  gchar *str = NULL;

  enum_class = g_type_class_ref (type);
  enum_value = g_enum_get_value (enum_class, value);

  if (enum_value)
    str = g_strdup (enum_value->value_nick);

  g_type_class_unref (enum_class);

  return str;
}

G_DEFINE_TYPE (GstWebRTCBinPad, gst_webrtc_bin_pad, GST_TYPE_GHOST_PAD);

static void
gst_webrtc_bin_pad_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_webrtc_bin_pad_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_webrtc_bin_pad_finalize (GObject * object)
{
  GstWebRTCBinPad *pad = GST_WEBRTC_BIN_PAD (object);

  if (pad->sender)
    gst_object_unref (pad->sender);
  pad->sender = NULL;
  if (pad->receiver)
    gst_object_unref (pad->receiver);
  pad->receiver = NULL;

  g_array_free (pad->ptmap, TRUE);

  G_OBJECT_CLASS (gst_webrtc_bin_pad_parent_class)->finalize (object);
}

static void
gst_webrtc_bin_pad_class_init (GstWebRTCBinPadClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;

  gobject_class->get_property = gst_webrtc_bin_pad_get_property;
  gobject_class->set_property = gst_webrtc_bin_pad_set_property;
  gobject_class->finalize = gst_webrtc_bin_pad_finalize;
}

typedef struct
{
  guint8 pt;
  GstCaps *caps;
} PtMapItem;

static void
clear_ptmap_item (PtMapItem * item)
{
  if (item->caps)
    gst_caps_unref (item->caps);
}

static GstCaps *
_pad_get_caps_for_pt (GstWebRTCBinPad * pad, guint pt)
{
  guint i, len;

  len = pad->ptmap->len;
  for (i = 0; i < len; i++) {
    PtMapItem *item = &g_array_index (pad->ptmap, PtMapItem, i);
    if (item->pt == pt)
      return item->caps;
  }
  return NULL;
}

static void
gst_webrtc_bin_pad_init (GstWebRTCBinPad * pad)
{
  pad->ptmap = g_array_new (FALSE, TRUE, sizeof (PtMapItem));
  g_array_set_clear_func (pad->ptmap, (GDestroyNotify) clear_ptmap_item);
}

static GstWebRTCBinPad *
gst_webrtc_bin_pad_new (const gchar * name, GstPadDirection direction)
{
  GstWebRTCBinPad *pad =
      g_object_new (gst_webrtc_bin_pad_get_type (), "name", name, "direction",
      direction, NULL);

  if (!gst_ghost_pad_construct (GST_GHOST_PAD (pad))) {
    gst_object_unref (pad);
    return NULL;
  }

  GST_DEBUG_OBJECT (pad, "new visible pad with direction %s",
      direction == GST_PAD_SRC ? "src" : "sink");
  return pad;
}

#define gst_webrtc_bin_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstWebRTCBin, gst_webrtc_bin, GST_TYPE_BIN,
    GST_DEBUG_CATEGORY_INIT (gst_webrtc_bin_debug, "webrtcbin", 0,
        "webrtcbin element");
    );

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink_%u",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS ("application/x-rtp"));

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src_%u",
    GST_PAD_SINK,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS ("application/x-rtp"));

enum
{
  SIGNAL_0,
  CREATE_OFFER_SIGNAL,
  CREATE_ANSWER_SIGNAL,
  SET_LOCAL_DESCRIPTION_SIGNAL,
  SET_REMOTE_DESCRIPTION_SIGNAL,
  ADD_ICE_CANDIDATE_SIGNAL,
  ON_NEGOTIATION_NEEDED_SIGNAL,
  ON_ICE_CANDIDATE_SIGNAL,
  ON_ICE_CANDIDATE_ERROR_SIGNAL,
  ON_FINGERPRINT_FAILURE_SIGNAL,
  LAST_SIGNAL,
};

enum
{
  PROP_0,
  PROP_CONNECTION_STATE,
  PROP_SIGNALING_STATE,
  PROP_ICE_GATHERING_STATE,
  PROP_ICE_CONNECTION_STATE,
  PROP_LOCAL_DESCRIPTION,
  PROP_CURRENT_LOCAL_DESCRIPTION,
  PROP_PENDING_LOCAL_DESCRIPTION,
  PROP_REMOTE_DESCRIPTION,
  PROP_CURRENT_REMOTE_DESCRIPTION,
  PROP_PENDING_REMOTE_DESCRIPTION,
  PROP_STUN_SERVER,
  PROP_TURN_SERVER,
};

static guint gst_webrtc_bin_signals[LAST_SIGNAL] = { 0 };

typedef struct
{
  guint session_id;
  GstWebRTCICEStream *stream;
} IceStreamItem;

/* FIXME: locking? */
GstWebRTCICEStream *
_find_ice_stream_for_session (GstWebRTCBin * webrtc, guint session_id)
{
  int i;

  for (i = 0; i < webrtc->priv->ice_stream_map->len; i++) {
    IceStreamItem *item =
        &g_array_index (webrtc->priv->ice_stream_map, IceStreamItem, i);

    if (item->session_id == session_id) {
      GST_TRACE_OBJECT (webrtc, "Found ice stream id %" GST_PTR_FORMAT " for "
          "session %u", item->stream, session_id);
      return item->stream;
    }
  }

  GST_TRACE_OBJECT (webrtc, "No ice stream available for session %u",
      session_id);
  return NULL;
}

void
_add_ice_stream_item (GstWebRTCBin * webrtc, guint session_id,
    GstWebRTCICEStream * stream)
{
  IceStreamItem item = { session_id, stream };

  GST_TRACE_OBJECT (webrtc, "adding ice stream %" GST_PTR_FORMAT " for "
      "session %u", stream, session_id);
  g_array_append_val (webrtc->priv->ice_stream_map, item);
}

typedef struct
{
  guint session_id;
  gchar *mid;
} SessionMidItem;

static void
clear_session_mid_item (SessionMidItem * item)
{
  g_free (item->mid);
}

static guint
_find_session_for_mid (GstWebRTCBin * webrtc, gchar * mid)
{
  int i;

  for (i = 0; i < webrtc->priv->session_mid_map->len; i++) {
    SessionMidItem *item =
        &g_array_index (webrtc->priv->session_mid_map, SessionMidItem, i);

    if (g_strcmp0 (item->mid, mid) == 0) {
      GST_TRACE_OBJECT (webrtc, "Found session %u for mid \'%s\'",
          item->session_id, mid);
      return item->session_id;
    }
  }

  GST_TRACE_OBJECT (webrtc, "No session available for mid \'%s\'", mid);
  return -1;
}

static void
_update_mid_session_id (GstWebRTCBin * webrtc, gchar * mid, guint session_id)
{
  SessionMidItem item;
  int i;

  for (i = 0; i < webrtc->priv->session_mid_map->len; i++) {
    SessionMidItem *m =
        &g_array_index (webrtc->priv->session_mid_map, SessionMidItem, i);

    if (g_strcmp0 (m->mid, mid) == 0) {
      GST_TRACE_OBJECT (webrtc, "Updating mid \'%s\' with session %u", mid,
          session_id);
      m->session_id = session_id;
      return;
    }
  }

  GST_TRACE_OBJECT (webrtc, "Adding mid \'%s\' with session %u", mid,
      session_id);
  item.mid = g_strdup (mid);
  item.session_id = session_id;
  g_array_append_val (webrtc->priv->session_mid_map, item);
}

typedef gboolean (*FindTransceiverFunc) (GstWebRTCRTPTransceiver * p1,
    gconstpointer data);

static GstWebRTCRTPTransceiver *
_find_transceiver (GstWebRTCBin * webrtc, gconstpointer data,
    FindTransceiverFunc func)
{
  int i;

  for (i = 0; i < webrtc->priv->transceivers->len; i++) {
    GstWebRTCRTPTransceiver *transceiver =
        g_array_index (webrtc->priv->transceivers, GstWebRTCRTPTransceiver *,
        i);

    if (func (transceiver, data))
      return transceiver;
  }

  return NULL;
}

static gboolean
match_for_mid (GstWebRTCRTPTransceiver * trans, const gchar * mid)
{
  return g_strcmp0 (trans->mid, mid) == 0;
}

static gboolean
match_for_mline (GstWebRTCRTPTransceiver * trans, guint * mline)
{
  return trans->mline == *mline;
}

static gboolean
match_stream_for_session (TransportStream * trans, guint * session)
{
  return trans->session_id == *session;
}

static TransportStream *
_find_transport_for_session (GstWebRTCBin * webrtc, guint session_id)
{
  TransportStream *stream;

  stream = TRANSPORT_STREAM (_find_transceiver (webrtc, &session_id,
          (FindTransceiverFunc) match_stream_for_session));

  GST_TRACE_OBJECT (webrtc,
      "Found transport %" GST_PTR_FORMAT " for session %u", stream, session_id);

  return stream;
}

typedef gboolean (*FindPadFunc) (GstWebRTCBinPad * p1, gconstpointer data);

static GstWebRTCBinPad *
_find_pad (GstWebRTCBin * webrtc, gconstpointer data, FindPadFunc func)
{
  GstElement *element = GST_ELEMENT (webrtc);
  GList *l;

  GST_OBJECT_LOCK (webrtc);
  l = element->pads;
  for (; l; l = g_list_next (l)) {
    if (!GST_IS_WEBRTC_BIN_PAD (l->data))
      continue;
    if (func (l->data, data)) {
      gst_object_ref (l->data);
      GST_OBJECT_UNLOCK (webrtc);
      return l->data;
    }
  }

  l = webrtc->priv->pending_pads;
  for (; l; l = g_list_next (l)) {
    if (!GST_IS_WEBRTC_BIN_PAD (l->data))
      continue;
    if (func (l->data, data)) {
      gst_object_ref (l->data);
      GST_OBJECT_UNLOCK (webrtc);
      return l->data;
    }
  }
  GST_OBJECT_UNLOCK (webrtc);

  return NULL;
}

static void
_add_pad_to_list (GstWebRTCBin * webrtc, GstWebRTCBinPad * pad)
{
  GST_OBJECT_LOCK (webrtc);
  webrtc->priv->pending_pads = g_list_prepend (webrtc->priv->pending_pads, pad);
  GST_OBJECT_UNLOCK (webrtc);
}

static void
_remove_pending_pad (GstWebRTCBin * webrtc, GstWebRTCBinPad * pad)
{
  GST_OBJECT_LOCK (webrtc);
  webrtc->priv->pending_pads = g_list_remove (webrtc->priv->pending_pads, pad);
  GST_OBJECT_UNLOCK (webrtc);
}

static void
_add_pad (GstWebRTCBin * webrtc, GstWebRTCBinPad * pad)
{
  _remove_pending_pad (webrtc, pad);

  if (webrtc->priv->running)
    gst_pad_set_active (GST_PAD (pad), TRUE);
  gst_element_add_pad (GST_ELEMENT (webrtc), GST_PAD (pad));
}

static void
_remove_pad (GstWebRTCBin * webrtc, GstWebRTCBinPad * pad)
{
  _remove_pending_pad (webrtc, pad);

  gst_element_remove_pad (GST_ELEMENT (webrtc), GST_PAD (pad));
}

typedef struct
{
  GstPadDirection direction;
  guint session_id;
} SessionMatch;

static gboolean
match_for_session (GstWebRTCBinPad * pad, const SessionMatch * match)
{
  return GST_PAD_DIRECTION (pad) == match->direction
      && pad->session_id == match->session_id;
}

typedef struct
{
  GstPadDirection direction;
  guint pt;
} PtMatch;

static gboolean
match_for_pt (GstWebRTCBinPad * pad, PtMatch * match)
{
  int i, len;

  if (GST_PAD_DIRECTION (pad) != match->direction)
    return FALSE;

  len = pad->ptmap->len;
  for (i = 0; i < len; i++) {
    PtMapItem *item = &g_array_index (pad->ptmap, PtMapItem, i);
    if (item->pt == match->pt)
      return TRUE;
  }

  return FALSE;
}

#if 0
static gboolean
match_for_ssrc (GstWebRTCBinPad * pad, guint * ssrc)
{
  return pad->ssrc == *ssrc;
}

static gboolean
match_for_pad (GstWebRTCBinPad * pad, GstWebRTCBinPad * other)
{
  return pad == other;
}
#endif

static gboolean
_unlock_pc_thread (GMutex * lock)
{
  g_mutex_unlock (lock);
  return G_SOURCE_REMOVE;
}

static gpointer
_gst_pc_thread (GstWebRTCBin * webrtc)
{
  PC_LOCK (webrtc);
  webrtc->priv->main_context = g_main_context_new ();
  webrtc->priv->loop = g_main_loop_new (webrtc->priv->main_context, FALSE);

  PC_COND_BROADCAST (webrtc);
  g_main_context_invoke (webrtc->priv->main_context,
      (GSourceFunc) _unlock_pc_thread, PC_GET_LOCK (webrtc));

  /* Having the thread be the thread default GMainContext will break the
   * required queue-like ordering (from W3's peerconnection spec) of re-entrant
   * tasks */
  g_main_loop_run (webrtc->priv->loop);

  PC_LOCK (webrtc);
  g_main_context_unref (webrtc->priv->main_context);
  webrtc->priv->main_context = NULL;
  g_main_loop_unref (webrtc->priv->loop);
  webrtc->priv->loop = NULL;
  PC_COND_BROADCAST (webrtc);
  PC_UNLOCK (webrtc);

  return NULL;
}

static void
_start_thread (GstWebRTCBin * webrtc)
{
  PC_LOCK (webrtc);
  webrtc->priv->thread = g_thread_new ("gst-pc-ops",
      (GThreadFunc) _gst_pc_thread, webrtc);

  while (!webrtc->priv->loop)
    PC_COND_WAIT (webrtc);
  webrtc->priv->is_closed = FALSE;
  PC_UNLOCK (webrtc);
}

static void
_stop_thread (GstWebRTCBin * webrtc)
{
  PC_LOCK (webrtc);
  webrtc->priv->is_closed = TRUE;
  g_main_loop_quit (webrtc->priv->loop);
  while (webrtc->priv->loop)
    PC_COND_WAIT (webrtc);
  PC_UNLOCK (webrtc);

  g_thread_unref (webrtc->priv->thread);
}

static gboolean
_execute_op (GstWebRTCBinTask * op)
{
  PC_LOCK (op->webrtc);
  if (op->webrtc->priv->is_closed) {
    GST_DEBUG_OBJECT (op->webrtc,
        "Peerconnection is closed, aborting execution");
    goto out;
  }

  op->op (op->webrtc, op->data);

out:
  PC_UNLOCK (op->webrtc);
  return G_SOURCE_REMOVE;
}

void
gst_webrtc_bin_enqueue_task (GstWebRTCBin * webrtc, GstWebRTCBinFunc func,
    gpointer data)
{
  GstWebRTCBinTask *op;
  GSource *source;

  g_return_if_fail (GST_IS_WEBRTC_BIN (webrtc));

  if (webrtc->priv->is_closed) {
    GST_DEBUG_OBJECT (webrtc, "Peerconnection is closed, aborting execution");
    return;
  }
  op = g_new0 (GstWebRTCBinTask, 1);
  op->webrtc = webrtc;
  op->op = func;
  op->data = data;

  source = g_idle_source_new ();
  g_source_set_priority (source, G_PRIORITY_DEFAULT);
  g_source_set_callback (source, (GSourceFunc) _execute_op, op, g_free);
  g_source_attach (source, webrtc->priv->main_context);
  g_source_unref (source);
}

static GstWebRTCRTPTransceiverDirection
_get_direction_from_media (const GstSDPMedia * media)
{
  GstWebRTCRTPTransceiverDirection new_dir =
      GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_NONE;
  int i;

  for (i = 0; i < gst_sdp_media_attributes_len (media); i++) {
    const GstSDPAttribute *attr = gst_sdp_media_get_attribute (media, i);

    if (g_strcmp0 (attr->key, "sendonly") == 0) {
      if (new_dir != GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_NONE) {
        GST_ERROR ("Multiple direction attributes");
        return GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_NONE;
      }
      new_dir = GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_SENDONLY;
    } else if (g_strcmp0 (attr->key, "sendrecv") == 0) {
      if (new_dir != GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_NONE) {
        GST_ERROR ("Multiple direction attributes");
        return GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_NONE;
      }
      new_dir = GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_SENDRECV;
    } else if (g_strcmp0 (attr->key, "recvonly") == 0) {
      if (new_dir != GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_NONE) {
        GST_ERROR ("Multiple direction attributes");
        return GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_NONE;
      }
      new_dir = GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_RECVONLY;
    } else if (g_strcmp0 (attr->key, "inactive") == 0) {
      if (new_dir != GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_NONE) {
        GST_ERROR ("Multiple direction attributes");
        return GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_NONE;
      }
      new_dir = GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_INACTIVE;
    }
  }

  return new_dir;
}

static GstWebRTCRTPTransceiverDirection
_intersect_answer_directions (GstWebRTCRTPTransceiverDirection offer,
    GstWebRTCRTPTransceiverDirection answer)
{
#define DIR(val) GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_ ## val

  if (offer == DIR (SENDONLY) && answer == DIR (SENDRECV))
    return DIR (RECVONLY);
  if (offer == DIR (SENDONLY) && answer == DIR (RECVONLY))
    return DIR (RECVONLY);
  if (offer == DIR (RECVONLY) && answer == DIR (SENDRECV))
    return DIR (SENDONLY);
  if (offer == DIR (RECVONLY) && answer == DIR (SENDONLY))
    return DIR (SENDONLY);
  if (offer == DIR (SENDRECV) && answer == DIR (SENDRECV))
    return DIR (SENDRECV);
  if (offer == DIR (SENDRECV) && answer == DIR (SENDONLY))
    return DIR (SENDONLY);
  if (offer == DIR (SENDRECV) && answer == DIR (RECVONLY))
    return DIR (RECVONLY);

  return DIR (NONE);

#undef DIR
}

#define SETUP(val) GST_WEBRTC_DTLS_SETUP_ ## val
static GstWebRTCDTLSSetup
_get_setup_from_media (const GstSDPMedia * media)
{
  int i;

  for (i = 0; i < gst_sdp_media_attributes_len (media); i++) {
    const GstSDPAttribute *attr = gst_sdp_media_get_attribute (media, i);

    if (g_strcmp0 (attr->key, "setup") == 0) {
      if (g_strcmp0 (attr->value, "actpass") == 0) {
        return SETUP (ACTPASS);
      } else if (g_strcmp0 (attr->value, "active") == 0) {
        return SETUP (ACTIVE);
      } else if (g_strcmp0 (attr->value, "passive") == 0) {
        return SETUP (PASSIVE);
      } else {
        GST_ERROR ("unknown setup value %s", attr->value);
        return SETUP (NONE);
      }
    }
  }

  GST_LOG ("no setup attribute in media");
  return SETUP (NONE);
}

static GstWebRTCDTLSSetup
_intersect_setup (GstWebRTCDTLSSetup offer)
{

  switch (offer) {
    case SETUP (NONE):         /* default is active */
    case SETUP (ACTPASS):
    case SETUP (PASSIVE):
      return SETUP (ACTIVE);
    case SETUP (ACTIVE):
      return SETUP (PASSIVE);
    default:
      return SETUP (NONE);
  }
}

#undef SETUP

static gchar *
_generate_fingerprint_from_certificate (gchar * certificate,
    GChecksumType checksum_type)
{
  gchar **lines, *line;
  guchar *tmp, *decoded, *digest;
  GChecksum *checksum;
  GString *fingerprint;
  gsize decoded_length, digest_size;
  gint state = 0;
  guint save = 0;
  int i;

  g_return_val_if_fail (certificate != NULL, NULL);

  /* 1. decode the certificate removing newlines and the certificate header
   * and footer */
  decoded = tmp = g_new0 (guchar, (strlen (certificate) / 4) * 3 + 3);
  lines = g_strsplit (certificate, "\n", 0);
  for (i = 0, line = lines[i]; line; line = lines[++i]) {
    if (line[0] && !g_str_has_prefix (line, "-----"))
      tmp += g_base64_decode_step (line, strlen (line), tmp, &state, &save);
  }
  g_strfreev (lines);
  decoded_length = tmp - decoded;

  /* 2. compute a checksum of the decoded certificate */
  checksum = g_checksum_new (checksum_type);
  digest_size = g_checksum_type_get_length (checksum_type);
  digest = g_new (guint8, digest_size);
  g_checksum_update (checksum, decoded, decoded_length);
  g_checksum_get_digest (checksum, digest, &digest_size);
  g_free (decoded);

  /* 3. hex encode the checksum separated with ':'s */
  fingerprint = g_string_new (NULL);
  for (i = 0; i < digest_size; i++) {
    if (i)
      g_string_append (fingerprint, ":");
    g_string_append_printf (fingerprint, "%02X", digest[i]);
  }

  g_free (digest);
  g_checksum_free (checksum);

  return g_string_free (fingerprint, FALSE);
}

/* https://www.w3.org/TR/webrtc/#dom-rtciceconnectionstate */
static GstWebRTCICEConnectionState
_collate_ice_connection_states (GstWebRTCBin * webrtc)
{
#define STATE(val) GST_WEBRTC_ICE_CONNECTION_STATE_ ## val
  GstWebRTCICEConnectionState any_state = 0;
  gboolean all_closed = TRUE;
  int i;

  /* trans->sender->transport and trans->receiver->transport are the same object */
  for (i = 0; i < webrtc->priv->transceivers->len; i++) {
    GstWebRTCRTPTransceiver *trans =
        g_array_index (webrtc->priv->transceivers, GstWebRTCRTPTransceiver *,
        i);
    TransportStream *stream = TRANSPORT_STREAM (trans);
    GstWebRTCICETransport *transport, *rtcp_transport;
    GstWebRTCICEConnectionState ice_state;
    gboolean rtcp_mux = FALSE;

    if (trans->stopped)
      continue;

    g_object_get (stream, "rtcp-mux", &rtcp_mux, NULL);

    transport = stream->transport->transport;

    /* get transport state */
    g_object_get (transport, "state", &ice_state, NULL);
    any_state |= (1 << ice_state);
    if (ice_state != STATE (CLOSED))
      all_closed = FALSE;

    rtcp_transport = stream->rtcp_transport->transport;

    if (!rtcp_mux && rtcp_transport && transport != rtcp_transport) {
      g_object_get (rtcp_transport, "state", &ice_state, NULL);
      any_state |= (1 << ice_state);
      if (ice_state != STATE (CLOSED))
        all_closed = FALSE;
    }
  }

  GST_TRACE_OBJECT (webrtc, "ICE connection state: 0x%x", any_state);

  if (webrtc->priv->is_closed) {
    GST_TRACE_OBJECT (webrtc, "returning closed");
    return STATE (CLOSED);
  }
  /* Any of the RTCIceTransport s are in the failed state. */
  if (any_state & (1 << STATE (FAILED))) {
    GST_TRACE_OBJECT (webrtc, "returning failed");
    return STATE (FAILED);
  }
  /* Any of the RTCIceTransport s are in the disconnected state and
   * none of them are in the failed state. */
  if (any_state & (1 << STATE (DISCONNECTED))) {
    GST_TRACE_OBJECT (webrtc, "returning disconnected");
    return STATE (DISCONNECTED);
  }
  /* Any of the RTCIceTransport's are in the checking state and none of them
   * are in the failed or disconnected state. */
  if (any_state & (1 << STATE (CHECKING))) {
    GST_TRACE_OBJECT (webrtc, "returning checking");
    return STATE (CHECKING);
  }
  /* Any of the RTCIceTransport s are in the new state and none of them are
   * in the checking, failed or disconnected state, or all RTCIceTransport's
   * are in the closed state. */
  if ((any_state & (1 << STATE (NEW))) || all_closed) {
    GST_TRACE_OBJECT (webrtc, "returning new");
    return STATE (NEW);
  }
  /* All RTCIceTransport s are in the connected, completed or closed state
   * and at least one of them is in the connected state. */
  if (any_state & (1 << STATE (CONNECTED) | 1 << STATE (COMPLETED) | 1 <<
          STATE (CLOSED)) && any_state & (1 << STATE (CONNECTED))) {
    GST_TRACE_OBJECT (webrtc, "returning connected");
    return STATE (CONNECTED);
  }
  /* All RTCIceTransport s are in the completed or closed state and at least
   * one of them is in the completed state. */
  if (any_state & (1 << STATE (COMPLETED) | 1 << STATE (CLOSED))
      && any_state & (1 << STATE (COMPLETED))) {
    GST_TRACE_OBJECT (webrtc, "returning connected");
    return STATE (CONNECTED);
  }

  GST_FIXME ("unspecified situation, returning new");
  return STATE (NEW);
#undef STATE
}

/* https://www.w3.org/TR/webrtc/#dom-rtcicegatheringstate */
static GstWebRTCICEGatheringState
_collate_ice_gathering_states (GstWebRTCBin * webrtc)
{
#define STATE(val) GST_WEBRTC_ICE_GATHERING_STATE_ ## val
  GstWebRTCICEGatheringState any_state = 0;
  gboolean all_completed = webrtc->priv->transceivers->len > 0;
  int i;

  /* trans->sender->transport and trans->receiver->transport are the same object */
  for (i = 0; i < webrtc->priv->transceivers->len; i++) {
    GstWebRTCRTPTransceiver *trans =
        g_array_index (webrtc->priv->transceivers, GstWebRTCRTPTransceiver *,
        i);
    TransportStream *stream = TRANSPORT_STREAM (trans);
    GstWebRTCICETransport *transport, *rtcp_transport;
    GstWebRTCICEGatheringState ice_state;
    gboolean rtcp_mux = FALSE;

    if (trans->stopped)
      continue;

    g_object_get (stream, "rtcp-mux", &rtcp_mux, NULL);

    transport = stream->transport->transport;

    /* get gathering state */
    g_object_get (transport, "gathering-state", &ice_state, NULL);
    any_state |= (1 << ice_state);
    if (ice_state != STATE (COMPLETE))
      all_completed = FALSE;

    rtcp_transport = stream->rtcp_transport->transport;

    if (!rtcp_mux && rtcp_transport && rtcp_transport != transport) {
      g_object_get (transport, "gathering-state", &ice_state, NULL);
      any_state |= (1 << ice_state);
      if (ice_state != STATE (COMPLETE))
        all_completed = FALSE;
    }
  }

  GST_TRACE_OBJECT (webrtc, "ICE gathering state: 0x%x", any_state);

  /* Any of the RTCIceTransport s are in the gathering state. */
  if (any_state & (1 << STATE (GATHERING))) {
    GST_TRACE_OBJECT (webrtc, "returning gathering");
    return STATE (GATHERING);
  }
  /* At least one RTCIceTransport exists, and all RTCIceTransport s are in
   * the completed gathering state. */
  if (all_completed) {
    GST_TRACE_OBJECT (webrtc, "returning complete");
    return STATE (COMPLETE);
  }

  /* Any of the RTCIceTransport s are in the new gathering state and none
   * of the transports are in the gathering state, or there are no transports. */
  GST_TRACE_OBJECT (webrtc, "returning new");
  return STATE (NEW);
#undef STATE
}

/* https://www.w3.org/TR/webrtc/#rtcpeerconnectionstate-enum */
static GstWebRTCPeerConnectionState
_collate_peer_connection_states (GstWebRTCBin * webrtc)
{
#define STATE(v) GST_WEBRTC_PEER_CONNECTION_STATE_ ## v
#define ICE_STATE(v) GST_WEBRTC_ICE_CONNECTION_STATE_ ## v
#define DTLS_STATE(v) GST_WEBRTC_DTLS_TRANSPORT_STATE_ ## v
  GstWebRTCICEConnectionState any_ice_state = 0;
  GstWebRTCDTLSTransportState any_dtls_state = 0;
  int i;

  /* trans->sender->transport and trans->receiver->transport are the same object */
  for (i = 0; i < webrtc->priv->transceivers->len; i++) {
    GstWebRTCRTPTransceiver *trans =
        g_array_index (webrtc->priv->transceivers, GstWebRTCRTPTransceiver *,
        i);
    TransportStream *stream = TRANSPORT_STREAM (trans);
    GstWebRTCDTLSTransport *transport, *rtcp_transport;
    GstWebRTCICEGatheringState ice_state;
    GstWebRTCDTLSTransportState dtls_state;
    gboolean rtcp_mux = FALSE;

    if (trans->stopped)
      continue;

    g_object_get (stream, "rtcp-mux", &rtcp_mux, NULL);
    transport = stream->transport;

    /* get transport state */
    g_object_get (transport, "state", &dtls_state, NULL);
    any_dtls_state |= (1 << dtls_state);
    g_object_get (transport->transport, "state", &ice_state, NULL);
    any_ice_state |= (1 << ice_state);

    rtcp_transport = stream->rtcp_transport;

    if (!rtcp_mux && rtcp_transport && rtcp_transport != transport) {
      g_object_get (rtcp_transport, "state", &dtls_state, NULL);
      any_dtls_state |= (1 << dtls_state);
      g_object_get (rtcp_transport->transport, "state", &ice_state, NULL);
      any_ice_state |= (1 << ice_state);
    }
  }

  GST_TRACE_OBJECT (webrtc, "ICE connection state: 0x%x. DTLS connection "
      "state: 0x%x", any_ice_state, any_dtls_state);

  /* The RTCPeerConnection object's [[ isClosed]] slot is true.  */
  if (webrtc->priv->is_closed) {
    GST_TRACE_OBJECT (webrtc, "returning closed");
    return STATE (CLOSED);
  }

  /* Any of the RTCIceTransport s or RTCDtlsTransport s are in a failed state. */
  if (any_ice_state & (1 << ICE_STATE (FAILED))) {
    GST_TRACE_OBJECT (webrtc, "returning failed");
    return STATE (FAILED);
  }
  if (any_dtls_state & (1 << DTLS_STATE (FAILED))) {
    GST_TRACE_OBJECT (webrtc, "returning failed");
    return STATE (FAILED);
  }

  /* Any of the RTCIceTransport's or RTCDtlsTransport's are in the connecting
   * or checking state and none of them is in the failed state. */
  if (any_ice_state & (1 << ICE_STATE (CHECKING))) {
    GST_TRACE_OBJECT (webrtc, "returning connecting");
    return STATE (CONNECTING);
  }
  if (any_dtls_state & (1 << DTLS_STATE (CONNECTING))) {
    GST_TRACE_OBJECT (webrtc, "returning connecting");
    return STATE (CONNECTING);
  }

  /* Any of the RTCIceTransport's or RTCDtlsTransport's are in the disconnected
   * state and none of them are in the failed or connecting or checking state. */
  if (any_ice_state & (1 << ICE_STATE (DISCONNECTED))) {
    GST_TRACE_OBJECT (webrtc, "returning disconnected");
    return STATE (DISCONNECTED);
  }

  /* All RTCIceTransport's and RTCDtlsTransport's are in the connected,
   * completed or closed state and at least of them is in the connected or
   * completed state. */
  if (!(any_ice_state & ~(1 << ICE_STATE (CONNECTED) | 1 <<
              ICE_STATE (COMPLETED) | 1 << ICE_STATE (CLOSED)))
      && !(any_dtls_state & ~(1 << DTLS_STATE (CONNECTED) | 1 <<
              DTLS_STATE (CLOSED)))
      && (any_ice_state & (1 << ICE_STATE (CONNECTED) | 1 <<
              ICE_STATE (COMPLETED))
          || any_dtls_state & (1 << DTLS_STATE (CONNECTED)))) {
    GST_TRACE_OBJECT (webrtc, "returning connected");
    return STATE (CONNECTED);
  }

  /* Any of the RTCIceTransport's or RTCDtlsTransport's are in the new state
   * and none of the transports are in the connecting, checking, failed or
   * disconnected state, or all transports are in the closed state. */
  if (!(any_ice_state & ~(1 << ICE_STATE (CLOSED)))) {
    GST_TRACE_OBJECT (webrtc, "returning new");
    return STATE (NEW);
  }
  if ((any_ice_state & (1 << ICE_STATE (NEW))
          || any_dtls_state & (1 << DTLS_STATE (NEW)))
      && !(any_ice_state & (1 << ICE_STATE (CHECKING) | 1 << ICE_STATE (FAILED)
              | (1 << ICE_STATE (DISCONNECTED))))
      && !(any_dtls_state & (1 << DTLS_STATE (CONNECTING) | 1 <<
              DTLS_STATE (FAILED)))) {
    GST_TRACE_OBJECT (webrtc, "returning new");
    return STATE (NEW);
  }

  GST_FIXME_OBJECT (webrtc, "Undefined situation detected, returning new");
  return STATE (NEW);
#undef DTLS_STATE
#undef ICE_STATE
#undef STATE
}

static void
_update_ice_gathering_state_task (GstWebRTCBin * webrtc, gpointer data)
{
  GstWebRTCICEGatheringState old_state = webrtc->ice_gathering_state;
  GstWebRTCICEGatheringState new_state;

  new_state = _collate_ice_gathering_states (webrtc);

  if (new_state != webrtc->ice_gathering_state) {
    gchar *old_s, *new_s;

    old_s = _enum_value_to_string (GST_TYPE_WEBRTC_ICE_GATHERING_STATE,
        old_state);
    new_s = _enum_value_to_string (GST_TYPE_WEBRTC_ICE_GATHERING_STATE,
        new_state);
    GST_INFO_OBJECT (webrtc, "ICE gathering state change from %s(%u) to %s(%u)",
        old_s, old_state, new_s, new_state);
    g_free (old_s);
    g_free (new_s);

    webrtc->ice_gathering_state = new_state;
    PC_UNLOCK (webrtc);
    g_object_notify (G_OBJECT (webrtc), "ice-gathering-state");
    PC_LOCK (webrtc);
  }
}

static void
_update_ice_gathering_state (GstWebRTCBin * webrtc)
{
  gst_webrtc_bin_enqueue_task (webrtc, _update_ice_gathering_state_task, NULL);
}

static void
_update_ice_connection_state_task (GstWebRTCBin * webrtc, gpointer data)
{
  GstWebRTCICEConnectionState old_state = webrtc->ice_connection_state;
  GstWebRTCICEConnectionState new_state;

  new_state = _collate_ice_connection_states (webrtc);

  if (new_state != old_state) {
    gchar *old_s, *new_s;

    old_s = _enum_value_to_string (GST_TYPE_WEBRTC_ICE_CONNECTION_STATE,
        old_state);
    new_s = _enum_value_to_string (GST_TYPE_WEBRTC_ICE_CONNECTION_STATE,
        new_state);
    GST_INFO_OBJECT (webrtc,
        "ICE connection state change from %s(%u) to %s(%u)", old_s, old_state,
        new_s, new_state);
    g_free (old_s);
    g_free (new_s);

    webrtc->ice_connection_state = new_state;
    PC_UNLOCK (webrtc);
    g_object_notify (G_OBJECT (webrtc), "ice-connection-state");
    PC_LOCK (webrtc);
  }
}

static void
_update_ice_connection_state (GstWebRTCBin * webrtc)
{
  gst_webrtc_bin_enqueue_task (webrtc, _update_ice_connection_state_task, NULL);
}

static void
_update_peer_connection_state_task (GstWebRTCBin * webrtc, gpointer data)
{
  GstWebRTCPeerConnectionState old_state = webrtc->peer_connection_state;
  GstWebRTCPeerConnectionState new_state;

  new_state = _collate_peer_connection_states (webrtc);

  if (new_state != old_state) {
    gchar *old_s, *new_s;

    old_s = _enum_value_to_string (GST_TYPE_WEBRTC_PEER_CONNECTION_STATE,
        old_state);
    new_s = _enum_value_to_string (GST_TYPE_WEBRTC_PEER_CONNECTION_STATE,
        new_state);
    GST_INFO_OBJECT (webrtc,
        "Peer connection state change from %s(%u) to %s(%u)", old_s, old_state,
        new_s, new_state);
    g_free (old_s);
    g_free (new_s);

    webrtc->peer_connection_state = new_state;
    PC_UNLOCK (webrtc);
    g_object_notify (G_OBJECT (webrtc), "connection-state");
    PC_LOCK (webrtc);
  }
}

static void
_update_peer_connection_state (GstWebRTCBin * webrtc)
{
  gst_webrtc_bin_enqueue_task (webrtc, _update_peer_connection_state_task,
      NULL);
}

/* http://w3c.github.io/webrtc-pc/#dfn-check-if-negotiation-is-needed */
static gboolean
_check_if_negotiation_is_needed (GstWebRTCBin * webrtc)
{
  int i;

  GST_LOG_OBJECT (webrtc, "checking if negotiation is needed");

  /* If any implementation-specific negotiation is required, as described at
   * the start of this section, return "true".
   * FIXME */
  /* FIXME: emit when input caps/format changes? */

  /* If connection has created any RTCDataChannel's, and no m= section has
   * been negotiated yet for data, return "true". 
   * FIXME */

  if (!webrtc->current_local_description) {
    GST_LOG_OBJECT (webrtc, "no local description set");
    return TRUE;
  }

  if (!webrtc->current_remote_description) {
    GST_LOG_OBJECT (webrtc, "no remote description set");
    return TRUE;
  }

  for (i = 0; i < webrtc->priv->transceivers->len; i++) {
    GstWebRTCRTPTransceiver *trans;

    trans =
        g_array_index (webrtc->priv->transceivers, GstWebRTCRTPTransceiver *,
        i);

    if (trans->stopped) {
      /* FIXME: If t is stopped and is associated with an m= section according to
       * [JSEP] (section 3.4.1.), but the associated m= section is not yet
       * rejected in connection's currentLocalDescription or
       * currentRemoteDescription , return "true". */
      GST_FIXME_OBJECT (webrtc,
          "check if the transceiver is rejected in descriptions");
    } else {
      const GstSDPMedia *media;
      GstWebRTCRTPTransceiverDirection local_dir, remote_dir;

      if (trans->mline == -1) {
        GST_LOG_OBJECT (webrtc, "unassociated transceiver %i %" GST_PTR_FORMAT,
            i, trans);
        return TRUE;
      }
      /* internal inconsistency */
      g_assert (trans->mline <
          gst_sdp_message_medias_len (webrtc->current_local_description->sdp));
      g_assert (trans->mline <
          gst_sdp_message_medias_len (webrtc->current_remote_description->sdp));

      /* FIXME: msid handling
       * If t's direction is "sendrecv" or "sendonly", and the associated m=
       * section in connection's currentLocalDescription doesn't contain an
       * "a=msid" line, return "true". */

      media =
          gst_sdp_message_get_media (webrtc->current_local_description->sdp,
          trans->mline);
      local_dir = _get_direction_from_media (media);

      media =
          gst_sdp_message_get_media (webrtc->current_remote_description->sdp,
          trans->mline);
      remote_dir = _get_direction_from_media (media);

      if (webrtc->current_local_description->type == GST_WEBRTC_SDP_TYPE_OFFER) {
        /* If connection's currentLocalDescription if of type "offer", and
         * the direction of the associated m= section in neither the offer
         * nor answer matches t's direction, return "true". */

        if (local_dir != trans->direction && remote_dir != trans->direction) {
          GST_LOG_OBJECT (webrtc,
              "transceiver direction doesn't match description");
          return TRUE;
        }
      } else if (webrtc->current_local_description->type ==
          GST_WEBRTC_SDP_TYPE_ANSWER) {
        GstWebRTCRTPTransceiverDirection intersect_dir;

        /* If connection's currentLocalDescription if of type "answer", and
         * the direction of the associated m= section in the answer does not
         * match t's direction intersected with the offered direction (as
         * described in [JSEP] (section 5.3.1.)), return "true". */

        /* remote is the offer, local is the answer */
        intersect_dir = _intersect_answer_directions (remote_dir, local_dir);

        if (intersect_dir != trans->direction) {
          GST_LOG_OBJECT (webrtc,
              "transceiver direction doesn't match description");
          return TRUE;
        }
      }
    }
  }

  GST_LOG_OBJECT (webrtc, "no negotiation needed");
  return FALSE;
}

static void
_check_need_negotiation_task (GstWebRTCBin * webrtc, gpointer unused)
{
  if (webrtc->priv->need_negotiation) {
    GST_TRACE_OBJECT (webrtc, "emitting on-negotiation-needed");
    g_signal_emit (webrtc, gst_webrtc_bin_signals[ON_NEGOTIATION_NEEDED_SIGNAL],
        0);
  }
}

/* http://w3c.github.io/webrtc-pc/#dfn-update-the-negotiation-needed-flag */
static void
_update_need_negotiation (GstWebRTCBin * webrtc)
{
  /* If connection's [[isClosed]] slot is true, abort these steps. */
  if (webrtc->priv->is_closed)
    return;
  /* If connection's signaling state is not "stable", abort these steps. */
  if (webrtc->signaling_state != GST_WEBRTC_SIGNALING_STATE_STABLE)
    return;

  /* If the result of checking if negotiation is needed is "false", clear the
   * negotiation-needed flag by setting connection's [[ needNegotiation]] slot
   * to false, and abort these steps. */
  if (!_check_if_negotiation_is_needed (webrtc)) {
    webrtc->priv->need_negotiation = FALSE;
    return;
  }
  /* If connection's [[needNegotiation]] slot is already true, abort these steps. */
  if (webrtc->priv->need_negotiation)
    return;
  /* Set connection's [[needNegotiation]] slot to true. */
  webrtc->priv->need_negotiation = TRUE;
  /* Queue a task to check connection's [[ needNegotiation]] slot and, if still
   * true, fire a simple event named negotiationneeded at connection. */
  gst_webrtc_bin_enqueue_task (webrtc, _check_need_negotiation_task, NULL);
}

static GstCaps *
_find_codec_preferences (GstWebRTCBin * webrtc, GstWebRTCRTPTransceiver * trans,
    GstPadDirection direction, guint media_idx)
{
  GstCaps *ret = NULL;

  GST_LOG_OBJECT (webrtc, "retreiving codec preferences from %" GST_PTR_FORMAT,
      trans);

  if (trans->codec_preferences) {
    GST_LOG_OBJECT (webrtc, "Using codec preferences: %" GST_PTR_FORMAT,
        trans->codec_preferences);
    ret = gst_caps_ref (trans->codec_preferences);
  } else {
    SessionMatch m = { direction, media_idx };
    GstWebRTCBinPad *pad =
        _find_pad (webrtc, &m, (FindPadFunc) match_for_session);
    if (pad) {
      GstCaps *caps = gst_pad_get_current_caps (GST_PAD (pad));
      if (caps) {
        GST_LOG_OBJECT (webrtc, "Using current pad caps: %" GST_PTR_FORMAT,
            caps);
      } else {
        if ((caps = gst_pad_peer_query_caps (GST_PAD (pad), NULL)))
          GST_LOG_OBJECT (webrtc, "Using peer query caps: %" GST_PTR_FORMAT,
              caps);
      }
      if (caps)
        ret = caps;
      gst_object_unref (pad);
    }
  }

  return ret;
}

static GstCaps *
_add_supported_attributes_to_caps (const GstCaps * caps)
{
  GstCaps *ret;
  int i;

  ret = gst_caps_make_writable (caps);

  for (i = 0; i < gst_caps_get_size (ret); i++) {
    GstStructure *s = gst_caps_get_structure (ret, i);

    if (!gst_structure_has_field (s, "rtcp-fb-nack"))
      gst_structure_set (s, "rtcp-fb-nack", G_TYPE_BOOLEAN, TRUE, NULL);
    if (!gst_structure_has_field (s, "rtcp-fb-nack-pli"))
      gst_structure_set (s, "rtcp-fb-nack-pli", G_TYPE_BOOLEAN, TRUE, NULL);
    /* FIXME: is this needed? */
    /*if (!gst_structure_has_field (s, "rtcp-fb-transport-cc"))
       gst_structure_set (s, "rtcp-fb-nack-pli", G_TYPE_BOOLEAN, TRUE, NULL); */

    /* FIXME: codec-specific paramters? */
  }

  return ret;
}

static const gchar *
_g_checksum_to_webrtc_string (GChecksumType type)
{
  switch (type) {
    case G_CHECKSUM_SHA1:
      return "sha-1";
    case G_CHECKSUM_SHA256:
      return "sha-256";
#ifdef G_CHECKSUM_SHA384
    case G_CHECKSUM_SHA384:
      return "sha-384";
#endif
    case G_CHECKSUM_SHA512:
      return "sha-512";
    default:
      g_warning ("unknown GChecksumType!");
      return NULL;
  }
}

static void
_on_ice_transport_notify_state (GstWebRTCICETransport * transport,
    GParamSpec * pspec, GstWebRTCBin * webrtc)
{
  _update_ice_connection_state (webrtc);
  _update_peer_connection_state (webrtc);
}

static void
_on_ice_transport_notify_gathering_state (GstWebRTCICETransport * transport,
    GParamSpec * pspec, GstWebRTCBin * webrtc)
{
  _update_ice_gathering_state (webrtc);
}

static void
_on_dtls_transport_notify_state (GstWebRTCDTLSTransport * transport,
    GParamSpec * pspec, GstWebRTCBin * webrtc)
{
  _update_peer_connection_state (webrtc);
}

static TransportStream *
_create_transport_channel (GstWebRTCBin * webrtc, guint session_id,
    guint mlineindex)
{
  GstWebRTCRTPTransceiver *trans;
  GstWebRTCRTPSender *sender;
  GstWebRTCRTPReceiver *receiver;
  TransportStream *ret;
  gchar *pad_name;

  /* FIXME: how to parametrize the sender and the receiver */
  sender = gst_webrtc_rtp_sender_new (NULL);
  receiver = gst_webrtc_rtp_receiver_new ();
  ret = transport_stream_new (webrtc, sender, receiver, session_id, mlineindex);
  trans = GST_WEBRTC_RTP_TRANSCEIVER (ret);
  trans->direction = GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_SENDRECV;

  g_signal_connect (G_OBJECT (ret->transport->transport), "notify::state",
      G_CALLBACK (_on_ice_transport_notify_state), webrtc);
  g_signal_connect (G_OBJECT (ret->transport->transport),
      "notify::gathering-state",
      G_CALLBACK (_on_ice_transport_notify_gathering_state), webrtc);
  g_signal_connect (G_OBJECT (ret->transport), "notify::state",
      G_CALLBACK (_on_dtls_transport_notify_state), webrtc);
  if (ret->transport != ret->rtcp_transport) {
    g_signal_connect (G_OBJECT (ret->rtcp_transport->transport),
        "notify::state", G_CALLBACK (_on_ice_transport_notify_state), webrtc);
    g_signal_connect (G_OBJECT (ret->rtcp_transport->transport),
        "notify::gathering-state",
        G_CALLBACK (_on_ice_transport_notify_gathering_state), webrtc);
    g_signal_connect (G_OBJECT (ret->rtcp_transport), "notify::state",
        G_CALLBACK (_on_dtls_transport_notify_state), webrtc);
  }

  gst_bin_add (GST_BIN (webrtc), GST_ELEMENT (ret->send_bin));
  gst_bin_add (GST_BIN (webrtc), GST_ELEMENT (ret->receive_bin));

  pad_name = g_strdup_printf ("recv_rtcp_sink_%u", ret->session_id);
  if (!gst_element_link_pads (GST_ELEMENT (ret->receive_bin), "rtcp_src",
          GST_ELEMENT (webrtc->rtpbin), pad_name))
    g_warn_if_reached ();
  g_free (pad_name);

  pad_name = g_strdup_printf ("send_rtcp_src_%u", ret->session_id);
  if (!gst_element_link_pads (GST_ELEMENT (webrtc->rtpbin), pad_name,
          GST_ELEMENT (ret->send_bin), "rtcp_sink"))
    g_warn_if_reached ();
  g_free (pad_name);

  g_array_append_val (webrtc->priv->transceivers, trans);

  GST_TRACE_OBJECT (webrtc,
      "Create transport %" GST_PTR_FORMAT " for session %u", ret, session_id);

  gst_element_sync_state_with_parent (GST_ELEMENT (ret->send_bin));
  gst_element_sync_state_with_parent (GST_ELEMENT (ret->receive_bin));

  gst_object_unref (sender);
  gst_object_unref (receiver);

  return ret;
}

/* based off https://tools.ietf.org/html/draft-ietf-rtcweb-jsep-18#section-5.2.1 */
static gboolean
sdp_media_from_transceiver (GstWebRTCBin * webrtc, GstSDPMedia * media,
    GstWebRTCRTPTransceiver * trans, GstWebRTCSDPType type, guint media_idx)
{
  /* TODO:
   * rtp header extensions
   * ice attributes
   * rtx
   * fec
   * msid-semantics
   * msid
   * dtls fingerprints
   * multiple dtls fingerprints https://tools.ietf.org/html/draft-ietf-mmusic-4572-update-05
   */
  gchar *direction, *sdp_mid;
  GstCaps *caps;
  int i;

  /* "An m= section is generated for each RtpTransceiver that has been added
   * to the Bin, excluding any stopped RtpTransceivers." */
  if (trans->stopped)
    return FALSE;
  if (trans->direction == GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_NONE
      || trans->direction == GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_INACTIVE)
    return FALSE;

  gst_sdp_media_set_port_info (media, 9, 0);
  gst_sdp_media_set_proto (media, "UDP/TLS/RTP/SAVPF");
  gst_sdp_media_add_connection (media, "IN", "IP4", "0.0.0.0", 0, 0);

  direction =
      _enum_value_to_string (GST_TYPE_WEBRTC_RTP_TRANSCEIVER_DIRECTION,
      trans->direction);
  gst_sdp_media_add_attribute (media, direction, "");
  g_free (direction);
  /* FIXME: negotiate this */
  gst_sdp_media_add_attribute (media, "rtcp-mux", "");

  if (type == GST_WEBRTC_SDP_TYPE_OFFER) {
    caps = _find_codec_preferences (webrtc, trans, GST_PAD_SINK, media_idx);
    caps = _add_supported_attributes_to_caps (caps);
  } else if (type == GST_WEBRTC_SDP_TYPE_ANSWER) {
    caps = _find_codec_preferences (webrtc, trans, GST_PAD_SRC, media_idx);
    /* FIXME: add rtcp-fb paramaters */
  } else {
    g_assert_not_reached ();
  }

  if (!caps || gst_caps_is_empty (caps) || gst_caps_is_any (caps)) {
    GST_WARNING_OBJECT (webrtc, "no caps available for transceiver, skipping");
    if (caps)
      gst_caps_unref (caps);
    return FALSE;
  }

  for (i = 0; i < gst_caps_get_size (caps); i++) {
    GstCaps *format = gst_caps_new_empty ();
    const GstStructure *s = gst_caps_get_structure (caps, i);

    gst_caps_append_structure (format, gst_structure_copy (s));

    GST_DEBUG_OBJECT (webrtc, "Adding %u-th caps %" GST_PTR_FORMAT
        " to %u-th media", i, format, media_idx);

    /* this only looks at the first structure so we loop over the given caps
     * and add each structure inside it piecemeal */
    gst_sdp_media_set_media_from_caps (format, media);

    gst_caps_unref (format);
  }

  /* Some identifier; we also add the media name to it so it's identifiable */
  sdp_mid = g_strdup_printf ("%s%u", gst_sdp_media_get_media (media),
      webrtc->priv->media_counter++);
  gst_sdp_media_add_attribute (media, "mid", sdp_mid);
  g_free (sdp_mid);

  if (trans->sender) {
    gchar *cert, *fingerprint, *val;

    g_object_get (trans->sender->transport, "certificate", &cert, NULL);

    fingerprint =
        _generate_fingerprint_from_certificate (cert, G_CHECKSUM_SHA256);
    g_free (cert);
    val =
        g_strdup_printf ("%s %s",
        _g_checksum_to_webrtc_string (G_CHECKSUM_SHA256), fingerprint);
    g_free (fingerprint);

    gst_sdp_media_add_attribute (media, "fingerprint", val);
    g_free (val);
  }

  gst_caps_unref (caps);

  return TRUE;
}

#if 0
static GList *
_transceiver_get_transports (GstWebRTCRTPTransceiver * trans)
{
  GList *ret = NULL;

  if (trans->sender) {
    if (trans->sender->transport) {
      if (g_list_find (ret, trans->sender->transport->transport) == NULL)
        ret = g_list_append (ret, trans->sender->transport->transport);
    }
    if (trans->sender->rtcp_transport) {
      if (g_list_find (ret, trans->sender->rtcp_transport->transport) == NULL)
        ret = g_list_append (ret, trans->sender->rtcp_transport->transport);
    }
  }
  if (trans->receiver) {
    if (trans->receiver->transport) {
      if (g_list_find (ret, trans->receiver->transport->transport) == NULL)
        ret = g_list_append (ret, trans->receiver->transport->transport);
    }
    if (trans->receiver->rtcp_transport) {
      if (g_list_find (ret, trans->receiver->rtcp_transport->transport) == NULL)
        ret = g_list_append (ret, trans->receiver->rtcp_transport->transport);
    }
  }

  return ret;
}
#endif
#define DEFAULT_ICE_UFRAG_LEN 32
#define DEFAULT_ICE_PASSWORD_LEN 32
static const gchar *ice_credential_chars =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ" "abcdefghijklmnopqrstuvwxyz" "0123456789" "+/";

static void
_generate_ice_credentials (gchar ** ufrag, gchar ** password)
{
  int i;

  *ufrag = g_malloc0 (DEFAULT_ICE_UFRAG_LEN + 1);
  for (i = 0; i < DEFAULT_ICE_UFRAG_LEN; i++)
    (*ufrag)[i] =
        ice_credential_chars[g_random_int_range (0,
            strlen (ice_credential_chars))];

  *password = g_malloc0 (DEFAULT_ICE_PASSWORD_LEN + 1);
  for (i = 0; i < DEFAULT_ICE_PASSWORD_LEN; i++)
    (*password)[i] =
        ice_credential_chars[g_random_int_range (0,
            strlen (ice_credential_chars))];
}

static GstSDPMessage *
_create_offer_task (GstWebRTCBin * webrtc, const GstStructure * options)
{
  GstSDPMessage *ret;
  int i;

  gst_sdp_message_new (&ret);

  gst_sdp_message_set_version (ret, "0");
  {
    /* FIXME: session id and version need special handling depending on the state we're in */
    gchar *sess_id = g_strdup_printf ("%" G_GUINT64_FORMAT, RANDOM_SESSION_ID);
    gst_sdp_message_set_origin (ret, "-", sess_id, "0", "IN", "IP4", "0.0.0.0");
    g_free (sess_id);
  }
  gst_sdp_message_set_session_name (ret, "-");
  gst_sdp_message_add_time (ret, "0", "0", NULL);
  gst_sdp_message_add_attribute (ret, "ice-options", "trickle");

  /* for each rtp transceiver */
  for (i = 0; i < webrtc->priv->transceivers->len; i++) {
    GstWebRTCRTPTransceiver *trans;
    GstSDPMedia media = { 0, };
    gchar *ufrag, *pwd;

    trans =
        g_array_index (webrtc->priv->transceivers, GstWebRTCRTPTransceiver *,
        i);

    gst_sdp_media_init (&media);
    /* mandated by JSEP */
    gst_sdp_media_add_attribute (&media, "setup", "actpass");

    /* FIXME: only needed when restarting ICE */
    _generate_ice_credentials (&ufrag, &pwd);
    gst_sdp_media_add_attribute (&media, "ice-ufrag", ufrag);
    gst_sdp_media_add_attribute (&media, "ice-pwd", pwd);
    g_free (ufrag);
    g_free (pwd);

    if (sdp_media_from_transceiver (webrtc, &media, trans,
            GST_WEBRTC_SDP_TYPE_OFFER, i))
      gst_sdp_message_add_media (ret, &media);
    else
      gst_sdp_media_uninit (&media);
  }

  /* FIXME: pre-emptively setup receiving elements when needed */

  /* XXX: only true for the initial offerer */
  g_object_set (webrtc->priv->ice, "controller", TRUE, NULL);

  return ret;
}

static void
_media_replace_direction (GstSDPMedia * media,
    GstWebRTCRTPTransceiverDirection direction)
{
  gchar *dir_str;
  int i;

  dir_str =
      _enum_value_to_string (GST_TYPE_WEBRTC_RTP_TRANSCEIVER_DIRECTION,
      direction);

  for (i = 0; i < gst_sdp_media_attributes_len (media); i++) {
    const GstSDPAttribute *attr = gst_sdp_media_get_attribute (media, i);

    if (g_strcmp0 (attr->key, "sendonly") == 0
        || g_strcmp0 (attr->key, "sendrecv") == 0
        || g_strcmp0 (attr->key, "recvonly") == 0) {
      GstSDPAttribute new_attr = { 0, };
      GST_TRACE ("replace %s with %s", attr->key, dir_str);
      gst_sdp_attribute_set (&new_attr, dir_str, "");
      gst_sdp_media_replace_attribute (media, i, &new_attr);
      return;
    }
  }

  GST_TRACE ("add %s", dir_str);
  gst_sdp_media_add_attribute (media, dir_str, "");
  g_free (dir_str);
}

static void
_media_replace_setup (GstSDPMedia * media, GstWebRTCDTLSSetup setup)
{
  gchar *setup_str;
  int i;

  setup_str = _enum_value_to_string (GST_TYPE_WEBRTC_DTLS_SETUP, setup);

  for (i = 0; i < gst_sdp_media_attributes_len (media); i++) {
    const GstSDPAttribute *attr = gst_sdp_media_get_attribute (media, i);

    if (g_strcmp0 (attr->key, "setup") == 0) {
      GstSDPAttribute new_attr = { 0, };
      GST_TRACE ("replace setup:%s with setup:%s", attr->value, setup_str);
      gst_sdp_attribute_set (&new_attr, "setup", setup_str);
      gst_sdp_media_replace_attribute (media, i, &new_attr);
      return;
    }
  }

  GST_TRACE ("add setup:%s", setup_str);
  gst_sdp_media_add_attribute (media, "setup", setup_str);
  g_free (setup_str);
}

static GstSDPMessage *
_create_answer_task (GstWebRTCBin * webrtc, const GstStructure * options)
{
  GstSDPMessage *ret = NULL;
  const GstWebRTCSessionDescription *pending_remote =
      webrtc->pending_remote_description;
  int i;

  if (!webrtc->pending_remote_description) {
    GST_ERROR_OBJECT (webrtc,
        "Asked to create an answer without a remote description");
    return NULL;
  }

  gst_sdp_message_new (&ret);

  /* FIXME: session id and version need special handling depending on the state we're in */
  gst_sdp_message_set_version (ret, "0");
  {
    const GstSDPOrigin *offer_origin =
        gst_sdp_message_get_origin (pending_remote->sdp);
    gst_sdp_message_set_origin (ret, "-", offer_origin->sess_id, "0", "IN",
        "IP4", "0.0.0.0");
  }
  gst_sdp_message_set_session_name (ret, "-");

  for (i = 0; i < gst_sdp_message_attributes_len (pending_remote->sdp); i++) {
    const GstSDPAttribute *attr =
        gst_sdp_message_get_attribute (pending_remote->sdp, i);

    if (g_strcmp0 (attr->key, "ice-options") == 0) {
      gst_sdp_message_add_attribute (ret, attr->key, attr->value);
    }
  }

  for (i = 0; i < gst_sdp_message_medias_len (pending_remote->sdp); i++) {
    /* FIXME:
     * bundle policy
     */
    GstSDPMedia *media = NULL;
    GstSDPMedia *offer_media;
    GstWebRTCRTPTransceiver *trans = NULL;
    GstWebRTCRTPTransceiverDirection offer_dir, answer_dir;
    GstWebRTCDTLSSetup offer_setup, answer_setup;
    GstCaps *offer_caps, *answer_caps = NULL;
    gchar *cert;
    int j;

    gst_sdp_media_new (&media);
    gst_sdp_media_set_port_info (media, 9, 0);
    gst_sdp_media_set_proto (media, "UDP/TLS/RTP/SAVPF");
    gst_sdp_media_add_connection (media, "IN", "IP4", "0.0.0.0", 0, 0);

    {
      /* FIXME: only needed when restarting ICE */
      gchar *ufrag, *pwd;
      _generate_ice_credentials (&ufrag, &pwd);
      gst_sdp_media_add_attribute (media, "ice-ufrag", ufrag);
      gst_sdp_media_add_attribute (media, "ice-pwd", pwd);
      g_free (ufrag);
      g_free (pwd);
    }

    offer_media =
        (GstSDPMedia *) gst_sdp_message_get_media (pending_remote->sdp, i);
    for (j = 0; j < gst_sdp_media_attributes_len (offer_media); j++) {
      const GstSDPAttribute *attr =
          gst_sdp_media_get_attribute (offer_media, j);

      if (g_strcmp0 (attr->key, "mid") == 0
          || g_strcmp0 (attr->key, "rtcp-mux") == 0) {
        gst_sdp_media_add_attribute (media, attr->key, attr->value);
        /* FIXME: handle anything we want to keep */
      }
    }

    offer_caps = gst_caps_new_empty ();
    for (j = 0; j < gst_sdp_media_formats_len (offer_media); j++) {
      guint pt = atoi (gst_sdp_media_get_format (offer_media, j));
      GstCaps *caps;
      int k;

      caps = gst_sdp_media_get_caps_from_media (offer_media, pt);

      /* gst_sdp_media_get_caps_from_media() produces caps with name
       * "application/x-unknown" which will fail intersection with
       * "application/x-rtp" caps so mangle the returns caps to have the
       * correct name here */
      for (k = 0; k < gst_caps_get_size (caps); k++) {
        GstStructure *s = gst_caps_get_structure (caps, k);
        gst_structure_set_name (s, "application/x-rtp");
      }

      gst_caps_append (offer_caps, caps);
    }

    for (j = 0; j < webrtc->priv->transceivers->len; j++) {
      GstCaps *trans_caps;

      trans =
          g_array_index (webrtc->priv->transceivers, GstWebRTCRTPTransceiver *,
          j);
      trans_caps = _find_codec_preferences (webrtc, trans, GST_PAD_SINK, i);

      GST_TRACE_OBJECT (webrtc, "trying to compare %" GST_PTR_FORMAT
          " and %" GST_PTR_FORMAT, offer_caps, trans_caps);

      /* FIXME: technically this is a little overreaching as some fields we
       * we can deal with not having and/or we may have unrecognized fields
       * that we cannot actually support */
      if (trans_caps) {
        answer_caps = gst_caps_intersect (offer_caps, trans_caps);
        if (answer_caps && !gst_caps_is_empty (answer_caps)) {
          GST_LOG_OBJECT (webrtc,
              "found compatible transceiver %" GST_PTR_FORMAT
              " for offer media %u", trans, i);
          if (trans_caps)
            gst_caps_unref (trans_caps);
          break;
        } else {
          if (answer_caps) {
            gst_caps_unref (answer_caps);
            answer_caps = NULL;
          }
          if (trans_caps)
            gst_caps_unref (trans_caps);
          trans = NULL;
        }
      } else {
        trans = NULL;
      }
    }

    if (trans) {
      answer_dir = trans->direction;
      if (!answer_caps)
        goto rejected;
    } else {
      /* if no transceiver, then we only receive that stream and respond with
       * the exact same caps */
      /* FIXME: how to validate that subsequent elements can actually receive
       * this payload/format */
      answer_dir = GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_RECVONLY;
      answer_caps = gst_caps_ref (offer_caps);
    }
    /* respond with the requested caps */
    if (answer_caps) {
      gst_sdp_media_set_media_from_caps (answer_caps, media);
      gst_caps_unref (answer_caps);
      answer_caps = NULL;
    }

    /* set the new media direction */
    offer_dir = _get_direction_from_media (offer_media);
    answer_dir = _intersect_answer_directions (offer_dir, answer_dir);
    if (answer_dir == GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_NONE) {
      GST_WARNING_OBJECT (webrtc, "Could not intersect offer direction with "
          "transceiver direction");
      goto rejected;
    }
    _media_replace_direction (media, answer_dir);

    /* set the a=setup: attribute */
    offer_setup = _get_setup_from_media (offer_media);
    answer_setup = _intersect_setup (offer_setup);
    if (answer_setup == GST_WEBRTC_DTLS_SETUP_NONE) {
      GST_WARNING_OBJECT (webrtc, "Could not intersect offer direction with "
          "transceiver direction");
      goto rejected;
    }
    _media_replace_setup (media, answer_setup);

    /* set the a=fingerprint: for this transport */
    if (!trans) {
      TransportStream *item = _find_transport_for_session (webrtc, i);
      if (!item)
        item = _create_transport_channel (webrtc, i, i);

      g_object_get (item->transport, "certificate", &cert, NULL);
    } else {
      g_object_get (trans->sender->transport, "certificate", &cert, NULL);
    }

    {
      gchar *fingerprint, *val;

      fingerprint =
          _generate_fingerprint_from_certificate (cert, G_CHECKSUM_SHA256);
      g_free (cert);
      val =
          g_strdup_printf ("%s %s",
          _g_checksum_to_webrtc_string (G_CHECKSUM_SHA256), fingerprint);
      g_free (fingerprint);

      gst_sdp_media_add_attribute (media, "fingerprint", val);
      g_free (val);
    }

    if (0) {
    rejected:
      GST_INFO_OBJECT (webrtc, "media %u rejected", i);
      gst_sdp_media_free (media);
      gst_sdp_media_copy (offer_media, &media);
      gst_sdp_media_set_port_info (media, 0, 0);
    }
    gst_sdp_message_add_media (ret, media);
    gst_sdp_media_free (media);

    gst_caps_unref (offer_caps);
  }

  /* FIXME: can we add not matched transceivers? */

  /* XXX: only true for the initial offerer */
  g_object_set (webrtc->priv->ice, "controller", FALSE, NULL);

  return ret;
}

struct create_sdp
{
  GstStructure *options;
  GstPromise *promise;
  GstWebRTCSDPType type;
};

static void
_create_sdp_task (GstWebRTCBin * webrtc, struct create_sdp *data)
{
  GstWebRTCSessionDescription *desc = NULL;
  GstSDPMessage *sdp = NULL;
  GstStructure *s = NULL;

  GST_INFO_OBJECT (webrtc, "creating %s sdp with options %" GST_PTR_FORMAT,
      gst_webrtc_sdp_type_to_string (data->type), data->options);

  if (data->type == GST_WEBRTC_SDP_TYPE_OFFER)
    sdp = _create_offer_task (webrtc, data->options);
  else if (data->type == GST_WEBRTC_SDP_TYPE_ANSWER)
    sdp = _create_answer_task (webrtc, data->options);
  else {
    g_assert_not_reached ();
    goto out;
  }

  if (sdp) {
    desc = gst_webrtc_session_description_new (data->type, sdp);
    s = gst_structure_new ("application/x-gst-promise",
        gst_webrtc_sdp_type_to_string (data->type),
        GST_TYPE_WEBRTC_SESSION_DESCRIPTION, desc, NULL);
  }

out:
  PC_UNLOCK (webrtc);
  gst_promise_reply (data->promise, s);
  PC_LOCK (webrtc);
  gst_promise_unref (data->promise);

  if (data->options)
    gst_structure_free (data->options);
  gst_webrtc_session_description_free (desc);
  g_free (data);
}

static void
gst_webrtc_bin_create_offer (GstWebRTCBin * webrtc,
    const GstStructure * options, GstPromise * promise)
{
  struct create_sdp *data = g_new0 (struct create_sdp, 1);

  if (options)
    data->options = gst_structure_copy (options);
  data->promise = gst_promise_ref (promise);
  data->type = GST_WEBRTC_SDP_TYPE_OFFER;

  gst_webrtc_bin_enqueue_task (webrtc, (GstWebRTCBinFunc) _create_sdp_task,
      data);
}

static void
gst_webrtc_bin_create_answer (GstWebRTCBin * webrtc,
    const GstStructure * options, GstPromise * promise)
{
  struct create_sdp *data = g_new0 (struct create_sdp, 1);

  if (options)
    data->options = gst_structure_copy (options);
  data->promise = gst_promise_ref (promise);
  data->type = GST_WEBRTC_SDP_TYPE_ANSWER;

  gst_webrtc_bin_enqueue_task (webrtc, (GstWebRTCBinFunc) _create_sdp_task,
      data);
}

typedef enum
{
  SDP_NONE,
  SDP_LOCAL,
  SDP_REMOTE,
} SDPSource;

static const gchar *
_sdp_source_to_string (SDPSource source)
{
  switch (source) {
    case SDP_LOCAL:
      return "local";
    case SDP_REMOTE:
      return "remote";
    default:
      return "none";
  }
}

static gboolean
_check_valid_state_for_sdp_change (GstWebRTCBin * webrtc, SDPSource source,
    GstWebRTCSDPType type, GError ** error)
{
  GstWebRTCSignalingState state = webrtc->signaling_state;
#define STATE(val) GST_WEBRTC_SIGNALING_STATE_ ## val
#define TYPE(val) GST_WEBRTC_SDP_TYPE_ ## val

  if (source == SDP_LOCAL && type == TYPE (OFFER) && state == STATE (STABLE))
    return TRUE;
  if (source == SDP_LOCAL && type == TYPE (OFFER)
      && state == STATE (HAVE_LOCAL_OFFER))
    return TRUE;
  if (source == SDP_LOCAL && type == TYPE (ANSWER)
      && state == STATE (HAVE_REMOTE_OFFER))
    return TRUE;
  if (source == SDP_LOCAL && type == TYPE (PRANSWER)
      && state == STATE (HAVE_REMOTE_OFFER))
    return TRUE;
  if (source == SDP_LOCAL && type == TYPE (PRANSWER)
      && state == STATE (HAVE_LOCAL_PRANSWER))
    return TRUE;

  if (source == SDP_REMOTE && type == TYPE (OFFER) && state == STATE (STABLE))
    return TRUE;
  if (source == SDP_REMOTE && type == TYPE (OFFER)
      && state == STATE (HAVE_REMOTE_OFFER))
    return TRUE;
  if (source == SDP_REMOTE && type == TYPE (ANSWER)
      && state == STATE (HAVE_LOCAL_OFFER))
    return TRUE;
  if (source == SDP_REMOTE && type == TYPE (PRANSWER)
      && state == STATE (HAVE_LOCAL_OFFER))
    return TRUE;
  if (source == SDP_REMOTE && type == TYPE (PRANSWER)
      && state == STATE (HAVE_REMOTE_PRANSWER))
    return TRUE;

  {
    gchar *state = _enum_value_to_string (GST_TYPE_WEBRTC_SIGNALING_STATE,
        webrtc->signaling_state);
    gchar *type_str = _enum_value_to_string (GST_TYPE_WEBRTC_SDP_TYPE, type);
    g_set_error (error, GST_WEBRTC_BIN_ERROR,
        GST_WEBRTC_BIN_ERROR_INVALID_STATE,
        "Not in the correct state (%s) for setting %s %s description", state,
        _sdp_source_to_string (source), type_str);
    g_free (state);
    g_free (type_str);
  }

  return FALSE;

#undef STATE
#undef TYPE
}

static gboolean
_check_sdp_crypto (GstWebRTCBin * webrtc, SDPSource source,
    GstWebRTCSessionDescription * sdp, GError ** error)
{
  const gchar *message_fingerprint, *fingerprint;
  const GstSDPKey *key;
  int i;

  key = gst_sdp_message_get_key (sdp->sdp);
  if (!IS_EMPTY_SDP_ATTRIBUTE (key->data)) {
    g_set_error_literal (error, GST_WEBRTC_BIN_ERROR,
        GST_WEBRTC_BIN_ERROR_BAD_SDP, "sdp contains a k line");
    return FALSE;
  }

  message_fingerprint = fingerprint =
      gst_sdp_message_get_attribute_val (sdp->sdp, "fingerprint");
  for (i = 0; i < gst_sdp_message_medias_len (sdp->sdp); i++) {
    const GstSDPMedia *media = gst_sdp_message_get_media (sdp->sdp, i);
    const gchar *media_fingerprint =
        gst_sdp_media_get_attribute_val (media, "fingerprint");

    if (!IS_EMPTY_SDP_ATTRIBUTE (message_fingerprint)
        && !IS_EMPTY_SDP_ATTRIBUTE (media_fingerprint)) {
      g_set_error (error, GST_WEBRTC_BIN_ERROR,
          GST_WEBRTC_BIN_ERROR_FINGERPRINT,
          "No fingerprint lines in sdp for media %u", i);
      return FALSE;
    }
    if (IS_EMPTY_SDP_ATTRIBUTE (fingerprint)) {
      fingerprint = media_fingerprint;
    }
    if (!IS_EMPTY_SDP_ATTRIBUTE (media_fingerprint)
        && g_strcmp0 (fingerprint, media_fingerprint) != 0) {
      g_set_error (error, GST_WEBRTC_BIN_ERROR,
          GST_WEBRTC_BIN_ERROR_FINGERPRINT,
          "Fingerprint in media %u differs from %s fingerprint. "
          "\'%s\' != \'%s\'", i, message_fingerprint ? "global" : "previous",
          fingerprint, media_fingerprint);
      return FALSE;
    }
  }

  return TRUE;
}

#if 0
static gboolean
_session_has_attribute_key (const GstSDPMessage * msg, const gchar * key)
{
  int i;
  for (i = 0; i < gst_sdp_message_attributes_len (msg); i++) {
    const GstSDPAttribute *attr = gst_sdp_message_get_attribute (msg, i);

    if (g_strcmp0 (attr->key, key) == 0)
      return TRUE;
  }

  return FALSE;
}

static gboolean
_session_has_attribute_key_value (const GstSDPMessage * msg, const gchar * key,
    const gchar * value)
{
  int i;
  for (i = 0; i < gst_sdp_message_attributes_len (msg); i++) {
    const GstSDPAttribute *attr = gst_sdp_message_get_attribute (msg, i);

    if (g_strcmp0 (attr->key, key) == 0 && g_strcmp0 (attr->value, value) == 0)
      return TRUE;
  }

  return FALSE;
}

static gboolean
_check_trickle_ice (GstSDPMessage * msg, GError ** error)
{
  if (!_session_has_attribute_key_value (msg, "ice-options", "trickle")) {
    g_set_error_literal (error, GST_WEBRTC_BIN_ERROR,
        GST_WEBRTC_BIN_ERROR_BAD_SDP,
        "No required \'a=ice-options:trickle\' line in sdp");
  }
  return TRUE;
}
#endif
static gboolean
_media_has_attribute_key (const GstSDPMedia * media, const gchar * key)
{
  int i;
  for (i = 0; i < gst_sdp_media_attributes_len (media); i++) {
    const GstSDPAttribute *attr = gst_sdp_media_get_attribute (media, i);

    if (g_strcmp0 (attr->key, key) == 0)
      return TRUE;
  }

  return FALSE;
}

static gboolean
_media_has_mid (const GstSDPMedia * media, guint media_idx, GError ** error)
{
  const gchar *mid = gst_sdp_media_get_attribute_val (media, "mid");
  if (IS_EMPTY_SDP_ATTRIBUTE (mid)) {
    g_set_error (error, GST_WEBRTC_BIN_ERROR, GST_WEBRTC_BIN_ERROR_BAD_SDP,
        "media %u is missing or contains an empty \'mid\' attribute",
        media_idx);
    return FALSE;
  }
  return TRUE;
}

static const gchar *
_media_get_ice_ufrag (const GstSDPMessage * msg, guint media_idx)
{
  const gchar *ice_ufrag;

  ice_ufrag = gst_sdp_message_get_attribute_val (msg, "ice-ufrag");
  if (IS_EMPTY_SDP_ATTRIBUTE (ice_ufrag)) {
    const GstSDPMedia *media = gst_sdp_message_get_media (msg, media_idx);
    ice_ufrag = gst_sdp_media_get_attribute_val (media, "ice-ufrag");
    if (IS_EMPTY_SDP_ATTRIBUTE (ice_ufrag))
      return NULL;
  }
  return ice_ufrag;
}

static const gchar *
_media_get_ice_pwd (const GstSDPMessage * msg, guint media_idx)
{
  const gchar *ice_pwd;

  ice_pwd = gst_sdp_message_get_attribute_val (msg, "ice-pwd");
  if (IS_EMPTY_SDP_ATTRIBUTE (ice_pwd)) {
    const GstSDPMedia *media = gst_sdp_message_get_media (msg, media_idx);
    ice_pwd = gst_sdp_media_get_attribute_val (media, "ice-pwd");
    if (IS_EMPTY_SDP_ATTRIBUTE (ice_pwd))
      return NULL;
  }
  return ice_pwd;
}

static gboolean
_media_has_setup (const GstSDPMedia * media, guint media_idx, GError ** error)
{
  static const gchar *valid_setups[] = { "actpass", "active", "passive", NULL };
  const gchar *setup = gst_sdp_media_get_attribute_val (media, "setup");
  if (IS_EMPTY_SDP_ATTRIBUTE (setup)) {
    g_set_error (error, GST_WEBRTC_BIN_ERROR, GST_WEBRTC_BIN_ERROR_BAD_SDP,
        "media %u is missing or contains an empty \'setup\' attribute",
        media_idx);
    return FALSE;
  }
  if (!g_strv_contains (valid_setups, setup)) {
    g_set_error (error, GST_WEBRTC_BIN_ERROR, GST_WEBRTC_BIN_ERROR_BAD_SDP,
        "media %u contains unknown \'setup\' attribute, \'%s\'", media_idx,
        setup);
    return FALSE;
  }
  return TRUE;
}

#if 0
static gboolean
_media_has_dtls_id (const GstSDPMedia * media, guint media_idx, GError ** error)
{
  const gchar *dtls_id = gst_sdp_media_get_attribute_val (media, "ice-pwd");
  if (IS_EMPTY_SDP_ATTRIBUTE (dtls_id)) {
    g_set_error (error, GST_WEBRTC_BIN_ERROR, GST_WEBRTC_BIN_ERROR_BAD_SDP,
        "media %u is missing or contains an empty \'dtls-id\' attribute",
        media_idx);
    return FALSE;
  }
  return TRUE;
}
#endif
static gboolean
validate_sdp (GstWebRTCBin * webrtc, SDPSource source,
    GstWebRTCSessionDescription * sdp, GError ** error)
{
#if 0
  const gchar *group, *bundle_ice_ufrag = NULL, *bundle_ice_pwd = NULL;
  gchar **group_members = NULL;
  gboolean is_bundle = FALSE;
#endif
  int i;

  if (!_check_valid_state_for_sdp_change (webrtc, source, sdp->type, error))
    return FALSE;
  if (!_check_sdp_crypto (webrtc, source, sdp, error))
    return FALSE;
/* not explicitly required
  if (ICE && !_check_trickle_ice (sdp->sdp))
    return FALSE;
  group = gst_sdp_message_get_attribute_val (sdp->sdp, "group");
  is_bundle = g_str_has_prefix (group, "BUNDLE");
  if (is_bundle)
    group_members = g_strsplit (&group[6], " ", -1);*/

  for (i = 0; i < gst_sdp_message_medias_len (sdp->sdp); i++) {
    const GstSDPMedia *media = gst_sdp_message_get_media (sdp->sdp, i);
#if 0
    const gchar *mid;
    gboolean media_in_bundle = FALSE, first_media_in_bundle = FALSE;
    gboolean bundle_only = FALSE;
#endif
    if (!_media_has_mid (media, i, error))
      goto fail;
#if 0
    mid = gst_sdp_media_get_attribute_val (media, "mid");
    media_in_bundle = is_bundle && g_strv_contains (group_members, mid);
    if (media_in_bundle)
      bundle_only =
          gst_sdp_media_get_attribute_val (media, "bundle-only") != NULL;
    first_media_in_bundle = media_in_bundle
        && g_strcmp0 (mid, group_members[0]) == 0;
#endif
    if (!_media_get_ice_ufrag (sdp->sdp, i)) {
      g_set_error (error, GST_WEBRTC_BIN_ERROR, GST_WEBRTC_BIN_ERROR_BAD_SDP,
          "media %u is missing or contains an empty \'ice-ufrag\' attribute",
          i);
      goto fail;
    }
    if (!_media_get_ice_pwd (sdp->sdp, i)) {
      g_set_error (error, GST_WEBRTC_BIN_ERROR, GST_WEBRTC_BIN_ERROR_BAD_SDP,
          "media %u is missing or contains an empty \'ice-pwd\' attribute", i);
      goto fail;
    }
    if (!_media_has_setup (media, i, error))
      goto fail;
#if 0
    /* check paramaters in bundle are the same */
    if (media_in_bundle) {
      const gchar *ice_ufrag =
          gst_sdp_media_get_attribute_val (media, "ice-ufrag");
      const gchar *ice_pwd = gst_sdp_media_get_attribute_val (media, "ice-pwd");
      if (!bundle_ice_ufrag)
        bundle_ice_ufrag = ice_ufrag;
      else if (!g_strcmp0 (bundle_ice_ufrag, ice_ufrag) != 0) {
        g_set_error (error, GST_WEBRTC_BIN_ERROR, GST_WEBRTC_BIN_ERROR_BAD_SDP,
            "media %u has different ice-ufrag values in bundle. "
            "%s != %s", i, bundle_ice_ufrag, ice_ufrag);
        goto fail;
      }
      if (!bundle_ice_pwd) {
        bundle_ice_pwd = ice_pwd;
      } else if (g_strcmp0 (bundle_ice_pwd, ice_pwd) == 0) {
        g_set_error (error, GST_WEBRTC_BIN_ERROR, GST_WEBRTC_BIN_ERROR_BAD_SDP,
            "media %u has different ice-ufrag values in bundle. "
            "%s != %s", i, bundle_ice_ufrag, ice_ufrag);
        goto fail;
      }
    }
#endif
  }

//  g_strv_free (group_members);

  return TRUE;

fail:
//  g_strv_free (group_members);
  return FALSE;
}

/*   m=<media> <UDP port> RTP/AVP <payload>
 */
static void
_update_pad_from_sdp_media (GstWebRTCBin * webrtc, const GstSDPMessage * sdp,
    guint media_idx, GstWebRTCBinPad * pad)
{
  guint i, len;
  const gchar *proto;
  GstCaps *global_caps;
  const GstSDPMedia *media;

  media = gst_sdp_message_get_media (sdp, media_idx);

  /* get proto */
  proto = gst_sdp_media_get_proto (media);
  if (proto == NULL)
    goto no_proto;

  /* Parse global SDP attributes once */
  global_caps = gst_caps_new_empty_simple ("application/x-unknown");
  GST_DEBUG_OBJECT (webrtc, "mapping sdp session level attributes to caps");
  gst_sdp_message_attributes_to_caps (sdp, global_caps);
  GST_DEBUG_OBJECT (webrtc, "mapping sdp media level attributes to caps");
  gst_sdp_media_attributes_to_caps (media, global_caps);

  /* clear the ptmap */
  g_array_set_size (pad->ptmap, 0);

  len = gst_sdp_media_formats_len (media);
  for (i = 0; i < len; i++) {
    int j;
    GstCaps *caps, *outcaps;
    GstStructure *s;
    PtMapItem item;
    gint pt;

    pt = atoi (gst_sdp_media_get_format (media, i));

    GST_DEBUG_OBJECT (webrtc, " looking at %d pt: %d", i, pt);

    /* convert caps */
    caps = gst_sdp_media_get_caps_from_media (media, pt);
    if (caps == NULL) {
      GST_WARNING_OBJECT (webrtc, " skipping pt %d without caps", pt);
      continue;
    }

    /* Merge in global caps */
    /* Intersect will merge in missing fields to the current caps */
    outcaps = gst_caps_intersect (caps, global_caps);
    gst_caps_unref (caps);

    /* the first pt will be the default */
    if (pad->ptmap->len == 0)
      pad->default_pt = pt;

    s = gst_caps_get_structure (outcaps, 0);
    gst_structure_set_name (s, "application/x-rtp");

    item.pt = pt;
    item.caps = outcaps;

    g_array_append_val (pad->ptmap, item);

    for (j = 0; j < gst_sdp_media_attributes_len (media); j++) {
      const GstSDPAttribute *attr = gst_sdp_media_get_attribute (media, j);

      GST_TRACE_OBJECT (pad, "media %u has attribute %u %s%s%s", media_idx, j,
          attr->key, !IS_EMPTY_SDP_ATTRIBUTE (attr->value) ? ":" : "",
          attr->value);
      if (g_strcmp0 (attr->key, "rtcp") == 0) {
        GST_LOG_OBJECT (pad, "supports rtcp");
        pad->rtcp = TRUE;
      }
      if (g_strcmp0 (attr->key, "rtcp-mux") == 0) {
        GST_LOG_OBJECT (pad, "supports rtcp-mux");
        pad->rtcp_mux = TRUE;
      }
      if (g_strcmp0 (attr->key, "rtcp-rsize") == 0) {
        GST_LOG_OBJECT (pad, "supports rtcp-rsize");
        pad->rtcp_rsize = TRUE;
      }
    }

    {
      GObject *session;
      g_signal_emit_by_name (webrtc->rtpbin, "get-internal-session",
          pad->session_id, &session);
      if (session) {
        g_object_set (session, "rtcp-reduced-size", pad->rtcp_rsize, NULL);
        g_object_unref (session);
      }
    }
  }

  gst_caps_unref (global_caps);
  return;

no_proto:
  {
    GST_ERROR_OBJECT (webrtc, "can't find proto in media");
    return;
  }
}

#if 0
static GstWebRTCBinPad *
_find_pad_for_sdp_media (GstWebRTCBin * webrtc,
    GstPadDirection direction, const GstSDPMessage * sdp, guint media_idx)
{
  const GstSDPMedia *media = gst_sdp_message_get_media (sdp, media_idx);
  GstWebRTCBinPad *pad;
  int i;

  for (i = 0; i < gst_sdp_media_formats_len (media); i++) {
    PtMatch m;

    m.direction = direction;
    m.pt = atoi (gst_sdp_media_get_format (media, i));

    if ((pad = _find_pad (webrtc, &m, (FindPadFunc) match_for_pt)))
      return pad;
  }

  return NULL;
}
#endif
static GstWebRTCBinPad *
_create_pad_for_sdp_media (GstWebRTCBin * webrtc, GstPadDirection direction,
    guint media_idx)
{
  GstWebRTCBinPad *pad;
  gchar *pad_name;

  pad_name =
      g_strdup_printf ("%s_%u", direction == GST_PAD_SRC ? "src" : "sink",
      media_idx);
  pad = gst_webrtc_bin_pad_new (pad_name, direction);
  g_free (pad_name);
  pad->session_id = media_idx;

  return pad;
}

static GstWebRTCRTPTransceiver *
_find_transceiver_for_sdp_media (GstWebRTCBin * webrtc,
    const GstSDPMessage * sdp, guint media_idx)
{
  const GstSDPMedia *media = gst_sdp_message_get_media (sdp, media_idx);
  GstWebRTCRTPTransceiver *ret = NULL;
  int i;

  for (i = 0; i < gst_sdp_media_attributes_len (media); i++) {
    const GstSDPAttribute *attr = gst_sdp_media_get_attribute (media, i);

    if (g_strcmp0 (attr->key, "mid") == 0) {
      if ((ret =
              _find_transceiver (webrtc, attr->value,
                  (FindTransceiverFunc) match_for_mid)))
        goto out;
    }
  }

  ret = _find_transceiver (webrtc, &media_idx,
      (FindTransceiverFunc) match_for_mline);

out:
  GST_TRACE_OBJECT (webrtc, "Found transceiver %" GST_PTR_FORMAT, ret);
  return ret;
}

static GstPad *
_connect_input_stream (GstWebRTCBin * webrtc, GstWebRTCBinPad * pad)
{
/*
 * ,-------------------------webrtcbin-------------------------,
 * ;                                                           ;
 * ;          ,-------rtpbin-------,   ,--transport_send_%u--, ;
 * ;          ;    send_rtp_src_%u o---o rtp_sink            ; ;
 * ;          ;                    ;   ;                     ; ;
 * ;          ;   send_rtcp_src_%u o---o rtcp_sink           ; ;
 * ; sink_%u  ;                    ;   '---------------------' ;
 * o----------o send_rtp_sink_%u   ;                           ;
 * ;          '--------------------'                           ;
 * '--------------------- -------------------------------------'
 */
  GstPadTemplate *rtp_templ;
  GstPad *rtp_sink;
  gchar *pad_name;
  TransportStream *item;

  GST_INFO_OBJECT (pad, "linking input stream %u", pad->session_id);

  rtp_templ =
      _find_pad_template (webrtc->rtpbin, GST_PAD_SINK, GST_PAD_REQUEST,
      "send_rtp_sink_%u");
  g_assert (rtp_templ);

  pad_name = g_strdup_printf ("send_rtp_sink_%u", pad->session_id);
  rtp_sink =
      gst_element_request_pad (webrtc->rtpbin, rtp_templ, pad_name, NULL);
  g_free (pad_name);
  gst_ghost_pad_set_target (GST_GHOST_PAD (pad), rtp_sink);
  gst_object_unref (rtp_sink);

  /* TODO: add scream in here */

  item = _find_transport_for_session (webrtc, pad->session_id);
  if (!item)
    item = _create_transport_channel (webrtc, pad->session_id, pad->session_id);

  pad->sender->transport = gst_object_ref (item->transport);

  pad_name = g_strdup_printf ("send_rtp_src_%u", pad->session_id);
  if (!gst_element_link_pads (GST_ELEMENT (webrtc->rtpbin), pad_name,
          GST_ELEMENT (item->send_bin), "rtp_sink"))
    g_warn_if_reached ();
  g_free (pad_name);

  if (pad->rtcp) {
    GObject *session;

    if (pad->rtcp_mux) {
      pad->sender->rtcp_transport = gst_object_ref (pad->sender->transport);
    } else {
      pad->sender->rtcp_transport = gst_object_ref (item->rtcp_transport);
    }

    g_signal_emit_by_name (webrtc->rtpbin, "get-internal-session",
        pad->session_id, &session);
    if (session) {
      g_object_set (session, "rtcp-reduced-size", pad->rtcp_rsize, NULL);
      g_object_unref (session);
    }
  }

  gst_element_sync_state_with_parent (GST_ELEMENT (item->send_bin));

  return GST_PAD (pad);
}

/* output pads are receiving elements */
static void
_create_output_network_transports (GstWebRTCBin * webrtc, GstWebRTCBinPad * pad)
{
/*
 * ,------------------------webrtcbin------------------------,
 * ;                             ,---------rtpbin---------,  ;
 * ; ,-transport_receive_%u--,   ;                        ;  ;
 * ; ;               rtp_src o---o recv_rtp_sink_%u       ;  ;
 * ; ;                       ;   ;                        ;  ;
 * ; ;              rtcp_src o---o recv_rtcp_sink_%u      ;  ;
 * ; '-----------------------'   ;                        ;  ; src_%u
 * ;                             ;  recv_rtp_src_%u_%u_%u o--o
 * ;                             '------------------------'  ;
 * '---------------------------------------------------------'
 */
  gchar *pad_name;
  TransportStream *item;

  item = _find_transport_for_session (webrtc, pad->session_id);
  if (!item)
    item = _create_transport_channel (webrtc, pad->session_id, pad->session_id);
  pad->receiver->transport = gst_object_ref (item->transport);

  pad_name = g_strdup_printf ("recv_rtp_sink_%u", pad->session_id);
  if (!gst_element_link_pads (GST_ELEMENT (item->receive_bin), "rtp_src",
          GST_ELEMENT (webrtc->rtpbin), pad_name))
    g_warn_if_reached ();
  g_free (pad_name);

  gst_element_sync_state_with_parent (GST_ELEMENT (item->receive_bin));

  if (!pad->rtcp) {
    pad->receiver->rtcp_transport = NULL;
  } else if (pad->rtcp_mux) {
    pad->receiver->rtcp_transport = gst_object_ref (pad->receiver->transport);
  } else {
    pad->receiver->rtcp_transport = gst_object_ref (item->rtcp_transport);
  }
}

static gboolean
match_first_receiver (GstWebRTCBinPad * pad, gconstpointer data)
{
  return pad->receiver != NULL;
}

static GstWebRTCBinPad *
_connect_output_stream (GstWebRTCBin * webrtc, GstWebRTCBinPad * pad)
{
  GST_INFO_OBJECT (pad, "linking output stream %u", pad->session_id);

  /* FIXME: bundle negotiation */
  if (webrtc->priv->bundle) {
    /* FIXME: locking for this... */
    GstWebRTCBinPad *other =
        _find_pad (webrtc, NULL, (FindPadFunc) match_first_receiver);
    if (other) {
      pad->receiver->transport = gst_object_ref (other->receiver->transport);
      /* rtcp_transport is always NULL when bundling */
      gst_object_unref (other);
    } else {
      _create_output_network_transports (webrtc, pad);
    }
  } else {
    _create_output_network_transports (webrtc, pad);
  }

  return pad;
}

typedef struct
{
  guint mlineindex;
  gchar *candidate;
} IceCandidateItem;

static void
_clear_ice_candidate_item (IceCandidateItem ** item)
{
  g_free ((*item)->candidate);
  g_free (*item);
}

static void
_add_ice_candidate (GstWebRTCBin * webrtc, IceCandidateItem * item)
{
  GstWebRTCICEStream *stream;

  stream = _find_ice_stream_for_session (webrtc, item->mlineindex);
  if (stream == NULL) {
    GST_WARNING_OBJECT (webrtc, "Unknown mline %u, ignoring", item->mlineindex);
    return;
  }

  GST_LOG_OBJECT (webrtc, "adding ICE candidate with mline:%u, %s",
      item->mlineindex, item->candidate);

  gst_webrtc_ice_add_candidate (webrtc->priv->ice, stream, item->candidate);
}

static void
_update_transceiver_from_sdp_media (GstWebRTCBin * webrtc,
    const GstSDPMessage * sdp, guint media_idx,
    GstWebRTCRTPTransceiver * transceiver)
{
  TransportStream *stream = TRANSPORT_STREAM (transceiver);
  GstWebRTCRTPTransceiverDirection prev_dir = transceiver->current_direction;
  GstWebRTCRTPTransceiverDirection new_dir;
  const GstSDPMedia *media = gst_sdp_message_get_media (sdp, media_idx);
  GstWebRTCDTLSSetup new_setup;
  gboolean new_rtcp_mux;
  int i;

  for (i = 0; i < gst_sdp_media_attributes_len (media); i++) {
    const GstSDPAttribute *attr = gst_sdp_media_get_attribute (media, i);

    if (g_strcmp0 (attr->key, "mid") == 0) {
      _update_mid_session_id (webrtc, attr->value, media_idx);

      g_free (transceiver->mid);
      transceiver->mid = g_strdup (attr->value);
    }
  }

  {
    const GstSDPMedia *local_media, *remote_media;
    GstWebRTCRTPTransceiverDirection local_dir, remote_dir;
    GstWebRTCDTLSSetup local_setup, remote_setup;
    gboolean local_rtcp_mux, remote_rtcp_mux;

    local_media =
        gst_sdp_message_get_media (webrtc->current_local_description->sdp,
        media_idx);
    remote_media =
        gst_sdp_message_get_media (webrtc->current_remote_description->sdp,
        media_idx);

    local_setup = _get_setup_from_media (local_media);
    remote_setup = _get_setup_from_media (remote_media);

#define SETUP(val) GST_WEBRTC_DTLS_SETUP_ ## val
    new_setup = SETUP (NONE);
    switch (local_setup) {
      case SETUP (NONE):
        /* someone's done a bad job of mangling the SDP. or bugs */
        g_critical ("Received a locally generated sdp without a parseable "
            "\'a=setup\' line.  This indicates a bug somewhere.  Bailing");
        return;
      case SETUP (ACTIVE):
        if (remote_setup == SETUP (ACTIVE)) {
          GST_ERROR_OBJECT (webrtc, "remote SDP has the same "
              "\'a=setup:active\' attribute. This is not legal");
          return;
        }
        new_setup = SETUP (ACTIVE);
        break;
      case SETUP (PASSIVE):
        if (remote_setup == SETUP (PASSIVE)) {
          GST_ERROR_OBJECT (webrtc, "remote SDP has the same "
              "\'a=setup:passive\' attribute. This is not legal");
          return;
        }
        new_setup = SETUP (PASSIVE);
        break;
      case SETUP (ACTPASS):
        if (remote_setup == SETUP (ACTPASS)) {
          GST_ERROR_OBJECT (webrtc, "remote SDP has the same "
              "\'a=setup:actpass\' attribute. This is not legal");
          return;
        }
        if (remote_setup == SETUP (ACTIVE))
          new_setup = SETUP (PASSIVE);
        else if (remote_setup == SETUP (PASSIVE))
          new_setup = SETUP (ACTIVE);
        else if (remote_setup == SETUP (NONE)) {
          /* XXX: what to do here? */
          GST_WARNING_OBJECT (webrtc, "unspecified situation. local: "
              "\'a=setup:actpass\' remote: none/unparseable");
          new_setup = SETUP (ACTIVE);
        }
        break;
      default:
        g_assert_not_reached ();
        return;
    }
    if (new_setup == SETUP (NONE)) {
      GST_ERROR_OBJECT (webrtc, "Abnormal situation!");
      return;
    }
#undef SETUP

    local_dir = _get_direction_from_media (local_media);
    remote_dir = _get_direction_from_media (remote_media);
#define DIR(val) GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_ ## val
    new_dir = DIR (NONE);
    switch (local_dir) {
      case DIR (INACTIVE):
        new_dir = DIR (INACTIVE);
        break;
      case DIR (SENDONLY):
        if (remote_dir == DIR (SENDONLY)) {
          GST_ERROR_OBJECT (webrtc, "remote SDP has the same directionality. "
              "This is not legal.");
          return;
        } else if (remote_dir == DIR (INACTIVE)) {
          new_dir = DIR (INACTIVE);
        } else {
          new_dir = DIR (SENDONLY);
        }
        break;
      case DIR (RECVONLY):
        if (remote_dir == DIR (RECVONLY)) {
          GST_ERROR_OBJECT (webrtc, "remote SDP has the same directionality. "
              "This is not legal.");
          return;
        } else if (remote_dir == DIR (INACTIVE)) {
          new_dir = DIR (INACTIVE);
        } else {
          new_dir = DIR (RECVONLY);
        }
        break;
      case DIR (SENDRECV):
        if (remote_dir == DIR (INACTIVE)) {
          new_dir = DIR (INACTIVE);
        } else if (remote_dir == DIR (SENDONLY)) {
          new_dir = DIR (RECVONLY);
        } else if (remote_dir == DIR (RECVONLY)) {
          new_dir = DIR (SENDONLY);
        } else if (remote_dir == DIR (SENDRECV)) {
          new_dir = DIR (SENDRECV);
        }
        break;
      default:
        g_assert_not_reached ();
        break;
    }

    if (new_dir == DIR (NONE)) {
      GST_ERROR_OBJECT (webrtc, "Abnormal situation!");
      return;
    }
#undef DIR

    local_rtcp_mux = _media_has_attribute_key (local_media, "rtcp-mux");
    remote_rtcp_mux = _media_has_attribute_key (remote_media, "rtcp-mux");

    new_rtcp_mux = local_rtcp_mux && remote_rtcp_mux;
  }

  if (prev_dir != GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_NONE
      && prev_dir != new_dir) {
    GST_FIXME_OBJECT (webrtc, "implement transceiver direction changes");
    return;
  }

  g_object_set (transceiver, "rtcp-mux", new_rtcp_mux, NULL);

  if (new_dir != prev_dir) {
    guint session_id = _find_session_for_mid (webrtc, transceiver->mid);
    TransportReceiveBin *receive = TRANSPORT_RECEIVE_BIN (stream->receive_bin);

    GST_TRACE_OBJECT (webrtc, "transceiver direction change");

    /* FIXME: this may not always be true */
    g_assert (media_idx == session_id);

    if (new_dir == GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_SENDONLY ||
        new_dir == GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_SENDRECV) {
      GstWebRTCBinPad *pad;
      SessionMatch m = { GST_PAD_SINK, session_id };

      pad = _find_pad (webrtc, &m, (FindPadFunc) match_for_session);
      if (pad) {
        GST_DEBUG_OBJECT (webrtc, "found existing send pad %" GST_PTR_FORMAT
            " for transceiver %" GST_PTR_FORMAT, pad, transceiver);
        gst_object_replace ((GstObject **) & transceiver->sender,
            (GstObject *) pad->sender);
        _update_pad_from_sdp_media (webrtc, sdp, media_idx, pad);
        gst_object_unref (pad);
      } else {
        GST_DEBUG_OBJECT (webrtc,
            "creating new pad send pad for transceiver %" GST_PTR_FORMAT,
            transceiver);
        pad = _create_pad_for_sdp_media (webrtc, GST_PAD_SINK, session_id);
        pad->sender = gst_object_ref (transceiver->sender);
        _connect_input_stream (webrtc, pad);
        _add_pad (webrtc, pad);
      }
      g_object_set (transceiver->sender->transport, "client",
          new_setup == GST_WEBRTC_DTLS_SETUP_ACTIVE, NULL);
      gst_element_set_locked_state (transceiver->sender->transport->dtlssrtpenc,
          FALSE);
      gst_element_sync_state_with_parent (transceiver->sender->
          transport->dtlssrtpenc);
    }
    if (new_dir == GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_RECVONLY ||
        new_dir == GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_SENDRECV) {
      GstWebRTCBinPad *pad;
      SessionMatch m = { GST_PAD_SRC, session_id };

      pad = _find_pad (webrtc, &m, (FindPadFunc) match_for_session);
      if (pad) {
        GST_DEBUG_OBJECT (webrtc, "found existing receive pad %" GST_PTR_FORMAT
            " for transceiver %" GST_PTR_FORMAT, pad, transceiver);
        gst_object_replace ((GstObject **) & transceiver->receiver,
            (GstObject *) pad->receiver);
        _update_pad_from_sdp_media (webrtc, sdp, media_idx, pad);
        gst_object_unref (pad);
      } else {
        GST_DEBUG_OBJECT (webrtc,
            "creating new receive pad for transceiver %" GST_PTR_FORMAT,
            transceiver);
        pad = _create_pad_for_sdp_media (webrtc, GST_PAD_SRC, session_id);
        _update_pad_from_sdp_media (webrtc, sdp, media_idx, pad);
        pad->receiver = gst_object_ref (transceiver->receiver);
        _connect_output_stream (webrtc, pad);
        /* delay adding the pad until rtpbin creates the recv output pad
         * to ghost to so queries/events travel through the pipeline correctly
         * as soon as the pad is added */
        _add_pad_to_list (webrtc, pad);
      }
      g_object_set (transceiver->receiver->transport, "client",
          new_setup == GST_WEBRTC_DTLS_SETUP_ACTIVE, NULL);
      gst_element_set_locked_state (transceiver->receiver->
          transport->dtlssrtpenc, FALSE);
      gst_element_sync_state_with_parent (transceiver->receiver->
          transport->dtlssrtpenc);
    }

    if (new_dir == GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_RECVONLY ||
        new_dir == GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_SENDRECV)
      transport_receive_bin_set_receive_state (receive, RECEIVE_STATE_PASS);
    else
      transport_receive_bin_set_receive_state (receive, RECEIVE_STATE_DROP);

    transceiver->mline = media_idx;
    transceiver->current_direction = new_dir;
  }
}

static gboolean
_update_transceivers_from_sdp (GstWebRTCBin * webrtc, SDPSource source,
    GstWebRTCSessionDescription * sdp)
{
  int i;

  for (i = 0; i < gst_sdp_message_medias_len (sdp->sdp); i++) {
    const GstSDPMedia *media = gst_sdp_message_get_media (sdp->sdp, i);
    GstWebRTCRTPTransceiver *trans;

    /* skip rejected media */
    if (gst_sdp_media_get_port (media) == 0)
      continue;

    trans = _find_transceiver_for_sdp_media (webrtc, sdp->sdp, i);

    if (source == SDP_LOCAL && sdp->type == GST_WEBRTC_SDP_TYPE_OFFER && !trans) {
      GST_ERROR ("State mismatch.  Could not find local transceiver by mline.");
      return FALSE;
    } else {
      if (trans) {
        _update_transceiver_from_sdp_media (webrtc, sdp->sdp, i, trans);
      } else {
        trans =
            GST_WEBRTC_RTP_TRANSCEIVER (_create_transport_channel (webrtc, i,
                i));
        /* XXX: default to the advertised direction in the sdp for new
         * transceviers.  The spec doesn't actually say what happens here, only
         * that calls to setDirection will change the value.  Nothing about
         * a default value when the transceiver is created internally */
        trans->direction = _get_direction_from_media (media);
        _update_transceiver_from_sdp_media (webrtc, sdp->sdp, i, trans);
      }
    }
  }

  return TRUE;
}

static void
_get_ice_credentials_from_sdp_media (const GstSDPMessage * sdp, guint media_idx,
    gchar ** ufrag, gchar ** pwd)
{
  int i;

  *ufrag = NULL;
  *pwd = NULL;

  {
    /* search in the corresponding media section */
    const GstSDPMedia *media = gst_sdp_message_get_media (sdp, media_idx);
    const gchar *tmp_ufrag =
        gst_sdp_media_get_attribute_val (media, "ice-ufrag");
    const gchar *tmp_pwd = gst_sdp_media_get_attribute_val (media, "ice-pwd");
    if (tmp_ufrag && tmp_pwd) {
      *ufrag = g_strdup (tmp_ufrag);
      *pwd = g_strdup (tmp_pwd);
      return;
    }
  }

  /* then in the sdp message itself */
  for (i = 0; i < gst_sdp_message_attributes_len (sdp); i++) {
    const GstSDPAttribute *attr = gst_sdp_message_get_attribute (sdp, i);

    if (g_strcmp0 (attr->key, "ice-ufrag") == 0) {
      g_assert (!*ufrag);
      *ufrag = g_strdup (attr->value);
    } else if (g_strcmp0 (attr->key, "ice-pwd") == 0) {
      g_assert (!*pwd);
      *pwd = g_strdup (attr->value);
    }
  }
  if (!*ufrag && !*pwd) {
    /* Check in the medias themselves. According to JSEP, they should be
     * identical FIXME: only for bundle-d streams */
    for (i = 0; i < gst_sdp_message_medias_len (sdp); i++) {
      const GstSDPMedia *media = gst_sdp_message_get_media (sdp, i);
      const gchar *tmp_ufrag =
          gst_sdp_media_get_attribute_val (media, "ice-ufrag");
      const gchar *tmp_pwd = gst_sdp_media_get_attribute_val (media, "ice-pwd");
      if (tmp_ufrag && tmp_pwd) {
        *ufrag = g_strdup (tmp_ufrag);
        *pwd = g_strdup (tmp_pwd);
        break;
      }
    }
  }
}

struct set_description
{
  GstPromise *promise;
  SDPSource source;
  GstWebRTCSessionDescription *sdp;
};

/* http://w3c.github.io/webrtc-pc/#set-description */
static void
_set_description_task (GstWebRTCBin * webrtc, struct set_description *sd)
{
  GstWebRTCSignalingState new_signaling_state = webrtc->signaling_state;
  GError *error = NULL;

  {
    gchar *state = _enum_value_to_string (GST_TYPE_WEBRTC_SIGNALING_STATE,
        webrtc->signaling_state);
    gchar *type_str =
        _enum_value_to_string (GST_TYPE_WEBRTC_SDP_TYPE, sd->sdp->type);
    gchar *sdp_text = gst_sdp_message_as_text (sd->sdp->sdp);
    GST_INFO_OBJECT (webrtc, "Attempting to set %s %s in the %s state",
        _sdp_source_to_string (sd->source), type_str, state);
    GST_TRACE_OBJECT (webrtc, "SDP contents\n%s", sdp_text);
    g_free (sdp_text);
    g_free (state);
    g_free (type_str);
  }

  if (!validate_sdp (webrtc, sd->source, sd->sdp, &error)) {
    GST_ERROR_OBJECT (webrtc, "%s", error->message);
    goto out;
  }

  if (webrtc->priv->is_closed) {
    GST_WARNING_OBJECT (webrtc, "we are closed");
    goto out;
  }

  switch (sd->sdp->type) {
    case GST_WEBRTC_SDP_TYPE_OFFER:{
      if (sd->source == SDP_LOCAL) {
        if (webrtc->pending_local_description)
          gst_webrtc_session_description_free
              (webrtc->pending_local_description);
        webrtc->pending_local_description = sd->sdp;
        new_signaling_state = GST_WEBRTC_SIGNALING_STATE_HAVE_LOCAL_OFFER;
      } else {
        if (webrtc->pending_remote_description)
          gst_webrtc_session_description_free
              (webrtc->pending_remote_description);
        webrtc->pending_remote_description = sd->sdp;
        new_signaling_state = GST_WEBRTC_SIGNALING_STATE_HAVE_REMOTE_OFFER;
      }
      break;
    }
    case GST_WEBRTC_SDP_TYPE_ANSWER:{
      if (sd->source == SDP_LOCAL) {
        if (webrtc->current_local_description)
          gst_webrtc_session_description_free
              (webrtc->current_local_description);
        webrtc->current_local_description = sd->sdp;

        if (webrtc->current_remote_description)
          gst_webrtc_session_description_free
              (webrtc->current_remote_description);
        webrtc->current_remote_description = webrtc->pending_remote_description;
        webrtc->pending_remote_description = NULL;
      } else {
        if (webrtc->current_remote_description)
          gst_webrtc_session_description_free
              (webrtc->current_remote_description);
        webrtc->current_remote_description = sd->sdp;

        if (webrtc->current_local_description)
          gst_webrtc_session_description_free
              (webrtc->current_local_description);
        webrtc->current_local_description = webrtc->pending_local_description;
        webrtc->pending_local_description = NULL;
      }

      if (webrtc->pending_local_description)
        gst_webrtc_session_description_free (webrtc->pending_local_description);
      webrtc->pending_local_description = NULL;

      if (webrtc->pending_remote_description)
        gst_webrtc_session_description_free
            (webrtc->pending_remote_description);
      webrtc->pending_remote_description = NULL;

      new_signaling_state = GST_WEBRTC_SIGNALING_STATE_STABLE;
      break;
    }
    case GST_WEBRTC_SDP_TYPE_ROLLBACK:{
      GST_FIXME_OBJECT (webrtc, "rollbacks are completely untested");
      if (sd->source == SDP_LOCAL) {
        if (webrtc->pending_local_description)
          gst_webrtc_session_description_free
              (webrtc->pending_local_description);
        webrtc->pending_local_description = NULL;
      } else {
        if (webrtc->pending_remote_description)
          gst_webrtc_session_description_free
              (webrtc->pending_remote_description);
        webrtc->pending_remote_description = NULL;
      }

      new_signaling_state = GST_WEBRTC_SIGNALING_STATE_STABLE;
      break;
    }
    case GST_WEBRTC_SDP_TYPE_PRANSWER:{
      GST_FIXME_OBJECT (webrtc, "pranswers are completely untested");
      if (sd->source == SDP_LOCAL) {
        if (webrtc->pending_local_description)
          gst_webrtc_session_description_free
              (webrtc->pending_local_description);
        webrtc->pending_local_description = sd->sdp;

        new_signaling_state = GST_WEBRTC_SIGNALING_STATE_HAVE_LOCAL_PRANSWER;
      } else {
        if (webrtc->pending_remote_description)
          gst_webrtc_session_description_free
              (webrtc->pending_remote_description);
        webrtc->pending_remote_description = sd->sdp;

        new_signaling_state = GST_WEBRTC_SIGNALING_STATE_HAVE_REMOTE_PRANSWER;
      }
      break;
    }
  }

  if (new_signaling_state != webrtc->signaling_state) {
    gchar *from = _enum_value_to_string (GST_TYPE_WEBRTC_SIGNALING_STATE,
        webrtc->signaling_state);
    gchar *to = _enum_value_to_string (GST_TYPE_WEBRTC_SIGNALING_STATE,
        new_signaling_state);
    GST_TRACE_OBJECT (webrtc, "notify signaling-state from %s "
        "to %s", from, to);
    webrtc->signaling_state = new_signaling_state;
    PC_UNLOCK (webrtc);
    g_object_notify (G_OBJECT (webrtc), "signaling-state");
    PC_LOCK (webrtc);

    g_free (from);
    g_free (to);
  }

  /* TODO: necessary data channel modifications */

  if (sd->sdp->type == GST_WEBRTC_SDP_TYPE_ROLLBACK) {
    /* FIXME:
     * If the mid value of an RTCRtpTransceiver was set to a non-null value 
     * by the RTCSessionDescription that is being rolled back, set the mid
     * value of that transceiver to null, as described by [JSEP]
     * (section 4.1.7.2.).
     * If an RTCRtpTransceiver was created by applying the
     * RTCSessionDescription that is being rolled back, and a track has not
     * been attached to it via addTrack, remove that transceiver from
     * connection's set of transceivers, as described by [JSEP]
     * (section 4.1.7.2.).
     * Restore the value of connection's [[ sctpTransport]] internal slot
     * to its value at the last stable signaling state.
     */
  }

  if (webrtc->signaling_state == GST_WEBRTC_SIGNALING_STATE_STABLE) {
    gboolean prev_need_negotiation = webrtc->priv->need_negotiation;

    /* media modifications */
    _update_transceivers_from_sdp (webrtc, sd->source, sd->sdp);

    /* If connection's signaling state is now stable, update the
     * negotiation-needed flag. If connection's [[ needNegotiation]] slot
     * was true both before and after this update, queue a task to check
     * connection's [[needNegotiation]] slot and, if still true, fire a
     * simple event named negotiationneeded at connection.*/
    _update_need_negotiation (webrtc);
    if (prev_need_negotiation && webrtc->priv->need_negotiation) {
      _check_need_negotiation_task (webrtc, NULL);
    }
  }

  if (sd->source == SDP_LOCAL) {
    int i;

    for (i = 0; i < gst_sdp_message_medias_len (sd->sdp->sdp); i++) {
      gchar *ufrag, *pwd;
      TransportStream *item;

      item = _find_transport_for_session (webrtc, i);
      if (!item)
        item = _create_transport_channel (webrtc, i, i);

      _get_ice_credentials_from_sdp_media (sd->sdp->sdp, i, &ufrag, &pwd);
      gst_webrtc_ice_set_local_credentials (webrtc->priv->ice,
          item->stream, ufrag, pwd);
      g_free (ufrag);
      g_free (pwd);
    }
  }

  if (sd->source == SDP_REMOTE) {
    int i;

    for (i = 0; i < gst_sdp_message_medias_len (sd->sdp->sdp); i++) {
      gchar *ufrag, *pwd;
      TransportStream *item;

      item = _find_transport_for_session (webrtc, i);
      if (!item)
        item = _create_transport_channel (webrtc, i, i);

      _get_ice_credentials_from_sdp_media (sd->sdp->sdp, i, &ufrag, &pwd);
      gst_webrtc_ice_set_remote_credentials (webrtc->priv->ice,
          item->stream, ufrag, pwd);
      g_free (ufrag);
      g_free (pwd);
    }
  }

  {
    int i;
    for (i = 0; i < webrtc->priv->ice_stream_map->len; i++) {
      IceStreamItem *item =
          &g_array_index (webrtc->priv->ice_stream_map, IceStreamItem, i);

      gst_webrtc_ice_gather_candidates (webrtc->priv->ice, item->stream);
    }
  }

  if (webrtc->current_local_description && webrtc->current_remote_description) {
    int i;

    for (i = 0; i < webrtc->priv->pending_ice_candidates->len; i++) {
      IceCandidateItem *item =
          g_array_index (webrtc->priv->pending_ice_candidates,
          IceCandidateItem *, i);

      _add_ice_candidate (webrtc, item);
    }
    g_array_set_size (webrtc->priv->pending_ice_candidates, 0);
  }

out:

  PC_UNLOCK (webrtc);
  gst_promise_reply (sd->promise, NULL);
  PC_LOCK (webrtc);
  gst_promise_unref (sd->promise);
  g_free (sd);
}

static void
gst_webrtc_bin_set_remote_description (GstWebRTCBin * webrtc,
    GstWebRTCSessionDescription * remote_sdp, GstPromise * promise)
{
  struct set_description *sd;

  if (remote_sdp == NULL)
    goto bad_input;
  if (remote_sdp->sdp == NULL)
    goto bad_input;

  sd = g_new0 (struct set_description, 1);
  sd->promise = gst_promise_ref (promise);
  sd->source = SDP_REMOTE;
  sd->sdp = gst_webrtc_session_description_copy (remote_sdp);

  gst_webrtc_bin_enqueue_task (webrtc, (GstWebRTCBinFunc) _set_description_task,
      sd);

  return;

bad_input:
  {
    gst_promise_reply (promise, NULL);
    g_return_if_reached ();
  }
}

static void
gst_webrtc_bin_set_local_description (GstWebRTCBin * webrtc,
    GstWebRTCSessionDescription * local_sdp, GstPromise * promise)
{
  struct set_description *sd;

  if (local_sdp == NULL)
    goto bad_input;
  if (local_sdp->sdp == NULL)
    goto bad_input;

  sd = g_new0 (struct set_description, 1);

  sd->promise = gst_promise_ref (promise);
  sd->source = SDP_LOCAL;
  sd->sdp = gst_webrtc_session_description_copy (local_sdp);

  gst_webrtc_bin_enqueue_task (webrtc, (GstWebRTCBinFunc) _set_description_task,
      sd);

  return;

bad_input:
  {
    gst_promise_reply (promise, NULL);
    g_return_if_reached ();
  }
}

static void
_add_ice_candidate_task (GstWebRTCBin * webrtc, IceCandidateItem * item)
{
  if (!webrtc->current_local_description || !webrtc->current_remote_description) {
    g_array_append_val (webrtc->priv->pending_ice_candidates, item);
  } else {
    _add_ice_candidate (webrtc, item);
    _clear_ice_candidate_item (&item);
  }
}

static void
gst_webrtc_bin_add_ice_candidate (GstWebRTCBin * webrtc, guint mline,
    const gchar * attr)
{
  IceCandidateItem *item;

  item = g_new0 (IceCandidateItem, 1);
  item->mlineindex = mline;
  if (!g_ascii_strncasecmp (attr, "a=candidate:", 12))
    item->candidate = g_strdup (attr);
  else if (!g_ascii_strncasecmp (attr, "candidate:", 10))
    item->candidate = g_strdup_printf ("a=%s", attr);
  gst_webrtc_bin_enqueue_task (webrtc,
      (GstWebRTCBinFunc) _add_ice_candidate_task, item);
}

static void
_on_ice_candidate_task (GstWebRTCBin * webrtc, IceCandidateItem * item)
{
  const gchar *cand = item->candidate;

  if (!g_ascii_strncasecmp (cand, "a=candidate:", 12)) {
    /* stripping away "a=" */
    cand += 2;
  }

  GST_TRACE_OBJECT (webrtc, "produced ICE candidate for mline:%u and %s",
      item->mlineindex, cand);

  PC_UNLOCK (webrtc);
  g_signal_emit (webrtc, gst_webrtc_bin_signals[ON_ICE_CANDIDATE_SIGNAL],
      0, item->mlineindex, cand);
  PC_LOCK (webrtc);

  g_free (item->candidate);
  g_free (item);
}

static void
_on_ice_candidate (GstWebRTCICE * ice, guint mlineindex,
    gchar * candidate, GstWebRTCBin * webrtc)
{
  IceCandidateItem *item = g_new0 (IceCandidateItem, 1);

  item->mlineindex = mlineindex;
  item->candidate = g_strdup (candidate);

  gst_webrtc_bin_enqueue_task (webrtc,
      (GstWebRTCBinFunc) _on_ice_candidate_task, item);
}

/* === rtpbin signal implementations === */

static void
on_rtpbin_pad_added (GstElement * rtpbin, GstPad * new_pad,
    GstWebRTCBin * webrtc)
{
  gchar *new_pad_name = NULL;

  new_pad_name = gst_pad_get_name (new_pad);
  GST_TRACE_OBJECT (webrtc, "new rtpbin pad %s", new_pad_name);
  if (g_str_has_prefix (new_pad_name, "recv_rtp_src_")) {
    guint32 session_id = 0, ssrc = 0, pt = 0;
    GstWebRTCBinPad *pad;
    PtMatch m;

    sscanf (new_pad_name, "recv_rtp_src_%u_%u_%u", &session_id, &ssrc, &pt);
    m.pt = pt;
    m.direction = GST_PAD_SRC;
    pad = _find_pad (webrtc, &m, (FindPadFunc) match_for_pt);
    GST_TRACE_OBJECT (webrtc, "found pad %" GST_PTR_FORMAT
        " for rtpbin pad name %s", pad, new_pad_name);
    pad->ssrc = ssrc;
    if (!pad)
      g_warn_if_reached ();
    gst_ghost_pad_set_target (GST_GHOST_PAD (pad), GST_PAD (new_pad));

    if (webrtc->priv->running)
      gst_pad_set_active (GST_PAD (pad), TRUE);
    gst_element_add_pad (GST_ELEMENT (webrtc), GST_PAD (pad));
    _remove_pending_pad (webrtc, pad);

    gst_object_unref (pad);
  }
  g_free (new_pad_name);
}

/* only used for the receiving streams */
static GstCaps *
on_rtpbin_request_pt_map (GstElement * rtpbin, guint session_id, guint pt,
    GstWebRTCBin * webrtc)
{
  GstWebRTCBinPad *pad;
  GstCaps *ret;
  PtMatch m = { GST_PAD_SRC, pt };

  GST_DEBUG_OBJECT (webrtc, "getting pt map for pt %d in session %d", pt,
      session_id);

  pad = _find_pad (webrtc, &m, (FindPadFunc) match_for_pt);
  if (!pad)
    goto unknown_session;

  if ((ret = _pad_get_caps_for_pt (pad, pt)))
    gst_caps_ref (ret);

  GST_TRACE_OBJECT (webrtc, "Found caps %" GST_PTR_FORMAT " for pt %d in "
      "session %d", ret, pt, session_id);

  gst_object_unref (pad);

  return ret;

unknown_session:
  {
    GST_DEBUG_OBJECT (webrtc, "unknown session %d", session_id);
    return NULL;
  }
}

static GstElement *
on_rtpbin_request_aux_sender (GstElement * rtpbin, guint session_id,
    GstWebRTCBin * webrtc)
{
  return NULL;
}

static GstElement *
on_rtpbin_request_aux_receiver (GstElement * rtpbin, guint session_id,
    GstWebRTCBin * webrtc)
{
  return NULL;
}

static void
on_rtpbin_ssrc_active (GstElement * rtpbin, guint session_id, guint ssrc,
    GstWebRTCBin * webrtc)
{
}

static void
on_rtpbin_new_jitterbuffer (GstElement * rtpbin, GstElement * jitterbuffer,
    guint session_id, guint ssrc, GstWebRTCBin * webrtc)
{
}

static GstElement *
_create_rtpbin (GstWebRTCBin * webrtc)
{
  GstElement *rtpbin;

  if (!(rtpbin = gst_element_factory_make ("rtpbin", "rtpbin")))
    return NULL;

  /* mandated by WebRTC */
  gst_util_set_object_arg (G_OBJECT (rtpbin), "rtp-profile", "savpf");

  g_signal_connect (rtpbin, "pad-added", G_CALLBACK (on_rtpbin_pad_added),
      webrtc);
  g_signal_connect (rtpbin, "request-pt-map",
      G_CALLBACK (on_rtpbin_request_pt_map), webrtc);
  g_signal_connect (rtpbin, "request-aux-sender",
      G_CALLBACK (on_rtpbin_request_aux_sender), webrtc);
  g_signal_connect (rtpbin, "request-aux-receiver",
      G_CALLBACK (on_rtpbin_request_aux_receiver), webrtc);
  g_signal_connect (rtpbin, "on-ssrc-active",
      G_CALLBACK (on_rtpbin_ssrc_active), webrtc);
  g_signal_connect (rtpbin, "new-jitterbuffer",
      G_CALLBACK (on_rtpbin_new_jitterbuffer), webrtc);

  return rtpbin;
}

static GstStateChangeReturn
gst_webrtc_bin_change_state (GstElement * element, GstStateChange transition)
{
  GstWebRTCBin *webrtc = GST_WEBRTC_BIN (element);
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  GST_DEBUG ("changing state: %s => %s",
      gst_element_state_get_name (GST_STATE_TRANSITION_CURRENT (transition)),
      gst_element_state_get_name (GST_STATE_TRANSITION_NEXT (transition)));

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (!webrtc->rtpbin) {
        /* FIXME: is this the right thing for a missing plugin? */
        GST_ELEMENT_ERROR (webrtc, CORE, MISSING_PLUGIN, (NULL), (NULL));
        return GST_STATE_CHANGE_FAILURE;
      }
      _update_need_negotiation (webrtc);
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      webrtc->priv->running = TRUE;
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      /* Mangle the return value to NO_PREROLL as that's what really is
       * occurring here however cannot be propagated correctly due to nicesrc
       * requiring that it be in PLAYING already in order to send/receive
       * correctly :/ */
      ret = GST_STATE_CHANGE_NO_PREROLL;
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      webrtc->priv->running = FALSE;
      break;
    default:
      break;
  }

  return ret;
}

static GstPad *
gst_webrtc_bin_request_new_pad (GstElement * element, GstPadTemplate * templ,
    const gchar * name, const GstCaps * caps)
{
  GstWebRTCBin *webrtc = GST_WEBRTC_BIN (element);
  GstWebRTCBinPad *pad = NULL;
  guint serial;

  if (templ->direction == GST_PAD_SINK ||
      g_strcmp0 (templ->name_template, "sink_%u") == 0) {
    GstWebRTCRTPTransceiver *trans;

    GST_OBJECT_LOCK (webrtc);
    if (name == NULL || strlen (name) < 6 || !g_str_has_prefix (name, "sink_")) {
      /* no name given when requesting the pad, use next available int */
      serial = webrtc->priv->max_sink_pad_serial++;
    } else {
      /* parse serial number from requested padname */
      serial = g_ascii_strtoull (&name[5], NULL, 10);
      if (serial > webrtc->priv->max_sink_pad_serial)
        webrtc->priv->max_sink_pad_serial = serial;
    }
    GST_OBJECT_UNLOCK (webrtc);

    pad = _create_pad_for_sdp_media (webrtc, GST_PAD_SINK, serial);
    trans =
        GST_WEBRTC_RTP_TRANSCEIVER (_create_transport_channel (webrtc, serial,
            serial));
    trans->direction = GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_SENDRECV;
    pad->sender = gst_object_ref (trans->sender);
    _connect_input_stream (webrtc, pad);

    /* TODO: update negotiation-needed */
    _add_pad (webrtc, pad);
  }

  return GST_PAD (pad);
}

static void
gst_webrtc_bin_release_pad (GstElement * element, GstPad * pad)
{
  GstWebRTCBin *webrtc = GST_WEBRTC_BIN (element);
  GstWebRTCBinPad *webrtc_pad = GST_WEBRTC_BIN_PAD (pad);

  if (webrtc_pad->sender)
    gst_object_unref (webrtc_pad->sender);
  webrtc_pad->sender = NULL;

  if (webrtc_pad->receiver)
    gst_object_unref (webrtc_pad->receiver);
  webrtc_pad->receiver = NULL;

  _remove_pad (webrtc, webrtc_pad);
}

static void
gst_webrtc_bin_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstWebRTCBin *webrtc = GST_WEBRTC_BIN (object);

  switch (prop_id) {
    case PROP_STUN_SERVER:
    case PROP_TURN_SERVER:
      g_object_set_property (G_OBJECT (webrtc->priv->ice), pspec->name, value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_webrtc_bin_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstWebRTCBin *webrtc = GST_WEBRTC_BIN (object);

  PC_LOCK (webrtc);
  switch (prop_id) {
    case PROP_CONNECTION_STATE:
      g_value_set_enum (value, webrtc->peer_connection_state);
      break;
    case PROP_SIGNALING_STATE:
      g_value_set_enum (value, webrtc->signaling_state);
      break;
    case PROP_ICE_GATHERING_STATE:
      g_value_set_enum (value, webrtc->ice_gathering_state);
      break;
    case PROP_ICE_CONNECTION_STATE:
      g_value_set_enum (value, webrtc->ice_connection_state);
      break;
    case PROP_LOCAL_DESCRIPTION:
      if (webrtc->pending_local_description)
        g_value_set_boxed (value, webrtc->pending_local_description);
      else if (webrtc->current_local_description)
        g_value_set_boxed (value, webrtc->current_local_description);
      else
        g_value_set_boxed (value, NULL);
      break;
    case PROP_CURRENT_LOCAL_DESCRIPTION:
      g_value_set_boxed (value, webrtc->current_local_description);
      break;
    case PROP_PENDING_LOCAL_DESCRIPTION:
      g_value_set_boxed (value, webrtc->pending_local_description);
      break;
    case PROP_REMOTE_DESCRIPTION:
      if (webrtc->pending_remote_description)
        g_value_set_boxed (value, webrtc->pending_remote_description);
      else if (webrtc->current_remote_description)
        g_value_set_boxed (value, webrtc->current_remote_description);
      else
        g_value_set_boxed (value, NULL);
      break;
    case PROP_CURRENT_REMOTE_DESCRIPTION:
      g_value_set_boxed (value, webrtc->current_remote_description);
      break;
    case PROP_PENDING_REMOTE_DESCRIPTION:
      g_value_set_boxed (value, webrtc->pending_remote_description);
      break;
    case PROP_STUN_SERVER:
    case PROP_TURN_SERVER:
      g_object_get_property (G_OBJECT (webrtc->priv->ice), pspec->name, value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  PC_UNLOCK (webrtc);
}

static void
_free_pending_pad (GstPad * pad)
{
  gst_object_unref (pad);
}

static void
gst_webrtc_bin_dispose (GObject * object)
{
  GstWebRTCBin *webrtc = GST_WEBRTC_BIN (object);

  _stop_thread (webrtc);

  if (webrtc->priv->ice)
    gst_object_unref (webrtc->priv->ice);
  webrtc->priv->ice = NULL;

  if (webrtc->priv->transceivers)
    g_array_free (webrtc->priv->transceivers, TRUE);
  webrtc->priv->transceivers = NULL;

  if (webrtc->priv->session_mid_map)
    g_array_free (webrtc->priv->session_mid_map, TRUE);
  webrtc->priv->session_mid_map = NULL;

  if (webrtc->priv->ice_stream_map)
    g_array_free (webrtc->priv->ice_stream_map, TRUE);
  webrtc->priv->ice_stream_map = NULL;

  if (webrtc->priv->pending_ice_candidates)
    g_array_free (webrtc->priv->pending_ice_candidates, TRUE);
  webrtc->priv->pending_ice_candidates = NULL;

  if (webrtc->priv->pending_pads)
    g_list_free_full (webrtc->priv->pending_pads,
        (GDestroyNotify) _free_pending_pad);
  webrtc->priv->pending_pads = NULL;

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_webrtc_bin_finalize (GObject * object)
{
  GstWebRTCBin *webrtc = GST_WEBRTC_BIN (object);

  if (webrtc->current_local_description)
    gst_webrtc_session_description_free (webrtc->current_local_description);
  webrtc->current_local_description = NULL;
  if (webrtc->pending_local_description)
    gst_webrtc_session_description_free (webrtc->pending_local_description);
  webrtc->pending_local_description = NULL;

  if (webrtc->current_remote_description)
    gst_webrtc_session_description_free (webrtc->current_remote_description);
  webrtc->current_remote_description = NULL;
  if (webrtc->pending_remote_description)
    gst_webrtc_session_description_free (webrtc->pending_remote_description);
  webrtc->pending_remote_description = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_webrtc_bin_class_init (GstWebRTCBinClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *element_class = (GstElementClass *) klass;

  g_type_class_add_private (klass, sizeof (GstWebRTCBinPrivate));

  element_class->request_new_pad = gst_webrtc_bin_request_new_pad;
  element_class->release_pad = gst_webrtc_bin_release_pad;
  element_class->change_state = gst_webrtc_bin_change_state;

  gst_element_class_add_static_pad_template (element_class, &sink_template);
  gst_element_class_add_static_pad_template (element_class, &src_template);

  gst_element_class_set_metadata (element_class, "WebRTC Bin",
      "Filter/Network/WebRTC", "A bin for webrtc connections",
      "Matthew Waters <matthew@centricular.com>");

  gobject_class->get_property = gst_webrtc_bin_get_property;
  gobject_class->set_property = gst_webrtc_bin_set_property;
  gobject_class->dispose = gst_webrtc_bin_dispose;
  gobject_class->finalize = gst_webrtc_bin_finalize;

  g_object_class_install_property (gobject_class,
      PROP_LOCAL_DESCRIPTION,
      g_param_spec_boxed ("local-description", "Local Description",
          "The local SDP description to use for this connection",
          GST_TYPE_WEBRTC_SESSION_DESCRIPTION,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
      PROP_REMOTE_DESCRIPTION,
      g_param_spec_boxed ("remote-description", "Remote Description",
          "The remote SDP description to use for this connection",
          GST_TYPE_WEBRTC_SESSION_DESCRIPTION,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
      PROP_STUN_SERVER,
      g_param_spec_string ("stun-server", "STUN Server",
          "The STUN server of the form stun://hostname:port",
          NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
      PROP_TURN_SERVER,
      g_param_spec_string ("turn-server", "TURN Server",
          "The TURN server of the form turn(s)://username:password@host:port",
          NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
      PROP_CONNECTION_STATE,
      g_param_spec_enum ("connection-state", "Connection State",
          "The overall connection state of this element",
          GST_TYPE_WEBRTC_PEER_CONNECTION_STATE,
          GST_WEBRTC_PEER_CONNECTION_STATE_NEW,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
      PROP_SIGNALING_STATE,
      g_param_spec_enum ("signaling-state", "Signaling State",
          "The signaling state of this element",
          GST_TYPE_WEBRTC_SIGNALING_STATE,
          GST_WEBRTC_SIGNALING_STATE_STABLE,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
      PROP_ICE_CONNECTION_STATE,
      g_param_spec_enum ("ice-connection-state", "ICE connection state",
          "The collective connection state of all ICETransport's",
          GST_TYPE_WEBRTC_ICE_CONNECTION_STATE,
          GST_WEBRTC_ICE_CONNECTION_STATE_NEW,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
      PROP_ICE_GATHERING_STATE,
      g_param_spec_enum ("ice-gathering-state", "ICE gathering state",
          "The collective gathering state of all ICETransport's",
          GST_TYPE_WEBRTC_ICE_GATHERING_STATE,
          GST_WEBRTC_ICE_GATHERING_STATE_NEW,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  /**
   * GstWebRTCBin::create-offer:
   * @object: the #GstWebRtcBin
   * @options: create-offer options
   *
   * Returns: a #GstSDPMessage offer
   */
  gst_webrtc_bin_signals[CREATE_OFFER_SIGNAL] =
      g_signal_new_class_handler ("create-offer", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      G_CALLBACK (gst_webrtc_bin_create_offer), NULL, NULL,
      g_cclosure_marshal_generic, G_TYPE_NONE, 2, GST_TYPE_STRUCTURE,
      GST_TYPE_PROMISE);

  /**
   * GstWebRTCBin::create-answer:
   * @object: the #GstWebRtcBin
   * @options: create-answer options
   *
   * Returns: a #GstSDPMessage answer
   */
  gst_webrtc_bin_signals[CREATE_ANSWER_SIGNAL] =
      g_signal_new_class_handler ("create-answer", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      G_CALLBACK (gst_webrtc_bin_create_answer), NULL, NULL,
      g_cclosure_marshal_generic, G_TYPE_NONE, 2, GST_TYPE_STRUCTURE,
      GST_TYPE_PROMISE);

  /**
   * GstWebRTCBin::set-local-description:
   * @object: the #GstWebRtcBin
   * @type: the type of description being set
   * @sdp: a #GstSDPMessage description
   *
   * Returns: a #GstSDPMessage offer
   */
  gst_webrtc_bin_signals[SET_LOCAL_DESCRIPTION_SIGNAL] =
      g_signal_new_class_handler ("set-local-description",
      G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      G_CALLBACK (gst_webrtc_bin_set_local_description), NULL, NULL,
      g_cclosure_marshal_generic, G_TYPE_NONE, 2,
      GST_TYPE_WEBRTC_SESSION_DESCRIPTION, GST_TYPE_PROMISE);

  /**
   * GstWebRTCBin::set-remote-description:
   * @object: the #GstWebRtcBin
   * @type: the type of description being set
   * @sdp: a #GstSDPMessage description
   */
  gst_webrtc_bin_signals[SET_REMOTE_DESCRIPTION_SIGNAL] =
      g_signal_new_class_handler ("set-remote-description",
      G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      G_CALLBACK (gst_webrtc_bin_set_remote_description), NULL, NULL,
      g_cclosure_marshal_generic, G_TYPE_NONE, 2,
      GST_TYPE_WEBRTC_SESSION_DESCRIPTION, GST_TYPE_PROMISE);

  /**
   * GstWebRTCBin::add-ice-candidate:
   * @object: the #GstWebRtcBin
   * @ice-candidate: an ice candidate
   */
  gst_webrtc_bin_signals[ADD_ICE_CANDIDATE_SIGNAL] =
      g_signal_new_class_handler ("add-ice-candidate",
      G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      G_CALLBACK (gst_webrtc_bin_add_ice_candidate), NULL, NULL,
      g_cclosure_marshal_generic, G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_STRING);

  /**
   * GstWebRTCBin::on-negotiation-needed:
   * @object: the #GstWebRtcBin
   */
  gst_webrtc_bin_signals[ON_NEGOTIATION_NEEDED_SIGNAL] =
      g_signal_new ("on-negotiation-needed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, g_cclosure_marshal_generic,
      G_TYPE_NONE, 0);

  /**
   * GstWebRTCBin::on-ice-candidate:
   * @object: the #GstWebRtcBin
   * @candidate: the ICE candidate
   */
  gst_webrtc_bin_signals[ON_ICE_CANDIDATE_SIGNAL] =
      g_signal_new ("on-ice-candidate", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, g_cclosure_marshal_generic,
      G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_STRING);
}

static void
_deref_and_unref (GObject ** object)
{
  gst_object_unref (*object);
}

static void
gst_webrtc_bin_init (GstWebRTCBin * webrtc)
{
  webrtc->priv =
      G_TYPE_INSTANCE_GET_PRIVATE ((webrtc), GST_TYPE_WEBRTC_BIN,
      GstWebRTCBinPrivate);

  _start_thread (webrtc);

  webrtc->rtpbin = _create_rtpbin (webrtc);
  gst_bin_add (GST_BIN (webrtc), webrtc->rtpbin);

  webrtc->priv->transceivers =
      g_array_new (FALSE, TRUE, sizeof (TransportStream *));
  g_array_set_clear_func (webrtc->priv->transceivers,
      (GDestroyNotify) _deref_and_unref);

  webrtc->priv->session_mid_map =
      g_array_new (FALSE, TRUE, sizeof (SessionMidItem));
  g_array_set_clear_func (webrtc->priv->session_mid_map,
      (GDestroyNotify) clear_session_mid_item);

  webrtc->priv->ice = gst_webrtc_ice_new ();
  g_signal_connect (webrtc->priv->ice, "on-ice-candidate",
      G_CALLBACK (_on_ice_candidate), webrtc);
  webrtc->priv->ice_stream_map =
      g_array_new (FALSE, TRUE, sizeof (IceStreamItem));
  webrtc->priv->pending_ice_candidates =
      g_array_new (FALSE, TRUE, sizeof (IceCandidateItem *));
  g_array_set_clear_func (webrtc->priv->pending_ice_candidates,
      (GDestroyNotify) _clear_ice_candidate_item);
}
