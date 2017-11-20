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

#ifndef __GST_WEBRTC_BIN_H__
#define __GST_WEBRTC_BIN_H__

#include <gst/gst.h>
#include <gst/sdp/sdp.h>
#include <gst/webrtc/webrtc.h>
#include "gstwebrtcice.h"

G_BEGIN_DECLS

#define GST_WEBRTC_BIN_ERROR gst_webrtc_bin_error_quark ()
GQuark gst_webrtc_bin_error_quark (void);

typedef enum
{
  GST_WEBRTC_BIN_ERROR_FAILED,
  GST_WEBRTC_BIN_ERROR_INVALID_SYNTAX,
  GST_WEBRTC_BIN_ERROR_INVALID_MODIFICATION,
  GST_WEBRTC_BIN_ERROR_INVALID_STATE,
  GST_WEBRTC_BIN_ERROR_BAD_SDP,
  GST_WEBRTC_BIN_ERROR_FINGERPRINT,
} GstWebRTCJSEPSDPError;

GType gst_webrtc_bin_pad_get_type(void);
#define GST_TYPE_WEBRTC_BIN_PAD            (gst_webrtc_bin_pad_get_type())
#define GST_WEBRTC_BIN_PAD(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_WEBRTC_BIN_PAD,GstWebRTCBinPad))
#define GST_IS_WEBRTC_BIN_PAD(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_WEBRTC_BIN_PAD))
#define GST_WEBRTC_BIN_PAD_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass) ,GST_TYPE_WEBRTC_BIN_PAD,GstWebRTCBinPadClass))
#define GST_IS_WEBRTC_BIN_PAD_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass) ,GST_TYPE_WEBRTC_BIN_PAD))
#define GST_WEBRTC_BIN_PAD_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj) ,GST_TYPE_WEBRTC_BIN_PAD,GstWebRTCBinPadClass))

typedef struct _GstWebRTCBinPad GstWebRTCBinPad;
typedef struct _GstWebRTCBinPadClass GstWebRTCBinPadClass;

struct _GstWebRTCBinPad
{
  GstGhostPad           parent;

  guint                 session_id;
  gboolean              rtcp;
  gboolean              rtcp_mux;
  gboolean              rtcp_rsize;

  /* only for receiving */
  GstWebRTCRTPReceiver *receiver;
  guint                 ssrc;
  guint                 default_pt;
  GArray               *ptmap;

  /* only for sending */
  GstElement           *payloader;
  GstWebRTCRTPSender   *sender;
};

struct _GstWebRTCBinPadClass
{
  GstGhostPadClass      parent_class;
};

GType gst_webrtc_bin_get_type(void);
#define GST_TYPE_WEBRTC_BIN            (gst_webrtc_bin_get_type())
#define GST_WEBRTC_BIN(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_WEBRTC_BIN,GstWebRTCBin))
#define GST_IS_WEBRTC_BIN(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_WEBRTC_BIN))
#define GST_WEBRTC_BIN_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass) ,GST_TYPE_WEBRTC_BIN,GstWebRTCBinClass))
#define GST_IS_WEBRTC_BIN_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass) ,GST_TYPE_WEBRTC_BIN))
#define GST_WEBRTC_BIN_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj) ,GST_TYPE_WEBRTC_BIN,GstWebRTCBinClass))

typedef struct _GstWebRTCBin GstWebRTCBin;
typedef struct _GstWebRTCBinClass GstWebRTCBinClass;
typedef struct _GstWebRTCBinPrivate GstWebRTCBinPrivate;

struct _GstWebRTCBin
{
  GstBin                            parent;

  GstElement                       *rtpbin;

  GstWebRTCSignallingState          signalling_state;
  GstWebRTCICEGatheringState        ice_gathering_state;
  GstWebRTCICEConnectionState       ice_connection_state;
  GstWebRTCPeerConnectionState      peer_connection_state;

  GstWebRTCSessionDescription      *current_local_description;
  GstWebRTCSessionDescription      *pending_local_description;
  GstWebRTCSessionDescription      *current_remote_description;
  GstWebRTCSessionDescription      *pending_remote_description;

  GstWebRTCBinPrivate              *priv;
};

struct _GstWebRTCBinClass
{
  GstBinClass           parent_class;
};

typedef void (*GstWebRTCBinFunc) (GstWebRTCBin * webrtc, gpointer data);

typedef struct
{
  GstWebRTCBin *webrtc;
  GstWebRTCBinFunc op;
  gpointer data;
//  GstPromise *promise;      /* FIXME */
} GstWebRTCBinTask;

void            gst_webrtc_bin_enqueue_task             (GstWebRTCBin * pc,
                                                         GstWebRTCBinFunc func,
                                                         gpointer data);

G_END_DECLS

#endif /* __GST_WEBRTC_BIN_H__ */
