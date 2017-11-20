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

#ifndef __GST_WEBRTC_FWD_H__
#define __GST_WEBRTC_FWD_H__

#include <gst/gst.h>
#include <gst/webrtc/webrtc-enumtypes.h>

typedef struct _GstWebRTCDTLSTransport GstWebRTCDTLSTransport;
typedef struct _GstWebRTCDTLSTransportClass GstWebRTCDTLSTransportClass;

typedef struct _GstWebRTCICETransport GstWebRTCICETransport;
typedef struct _GstWebRTCICETransportClass GstWebRTCICETransportClass;

typedef struct _GstWebRTCRTPReceiver GstWebRTCRTPReceiver;
typedef struct _GstWebRTCRTPReceiverClass GstWebRTCRTPReceiverClass;

typedef struct _GstWebRTCRTPSender GstWebRTCRTPSender;
typedef struct _GstWebRTCRTPSenderClass GstWebRTCRTPSenderClass;

typedef struct _GstWebRTCSessionDescription GstWebRTCSessionDescription;

typedef struct _GstWebRTCRTPTransceiver GstWebRTCRTPTransceiver;
typedef struct _GstWebRTCRTPTransceiverClass GstWebRTCRTPTransceiverClass;

typedef struct _GstWebRTCUDPTransport GstWebRTCUDPTransport;
typedef struct _GstWebRTCUDPTransportClass GstWebRTCUDPTransportClass;
typedef struct _GstWebRTCUDPTransportPrivate GstWebRTCUDPTransportPrivate;

/**
 * GstWebRTCDTLSTransportState:
 * GST_WEBRTC_DTLS_TRANSPORT_STATE_NEW: new
 * GST_WEBRTC_DTLS_TRANSPORT_STATE_CLOSED: closed
 * GST_WEBRTC_DTLS_TRANSPORT_STATE_FAILED: failed
 * GST_WEBRTC_DTLS_TRANSPORT_STATE_CONNECTING: connecting
 * GST_WEBRTC_DTLS_TRANSPORT_STATE_CONNECTED: connected
 */
typedef enum /*< underscore_name=gst_webrtc_dtls_transport_state >*/
{
  GST_WEBRTC_DTLS_TRANSPORT_STATE_NEW,
  GST_WEBRTC_DTLS_TRANSPORT_STATE_CLOSED,
  GST_WEBRTC_DTLS_TRANSPORT_STATE_FAILED,
  GST_WEBRTC_DTLS_TRANSPORT_STATE_CONNECTING,
  GST_WEBRTC_DTLS_TRANSPORT_STATE_CONNECTED,
} GstWebRTCDTLSTransportState;

/**
 * GstWebRTCICEGatheringState:
 * GST_WEBRTC_ICE_GATHERING_STATE_NEW: new
 * GST_WEBRTC_ICE_GATHERING_STATE_GATHERING: gathering
 * GST_WEBRTC_ICE_GATHERING_STATE_COMPLETE: complete
 *
 * See http://w3c.github.io/webrtc-pc/#dom-rtcicegatheringstate
 */
typedef enum /*< underscore_name=gst_webrtc_ice_gathering_state >*/
{
  GST_WEBRTC_ICE_GATHERING_STATE_NEW,
  GST_WEBRTC_ICE_GATHERING_STATE_GATHERING,
  GST_WEBRTC_ICE_GATHERING_STATE_COMPLETE,
} GstWebRTCICEGatheringState; /*< underscore_name=gst_webrtc_ice_gathering_state >*/

/**
 * GstWebRTCICEConnectionState:
 * GST_WEBRTC_ICE_CONNECTION_STATE_NEW: new
 * GST_WEBRTC_ICE_CONNECTION_STATE_CHECKING: checking
 * GST_WEBRTC_ICE_CONNECTION_STATE_CONNECTED: connected
 * GST_WEBRTC_ICE_CONNECTION_STATE_COMPLETED: completed
 * GST_WEBRTC_ICE_CONNECTION_STATE_FAILED: failed
 * GST_WEBRTC_ICE_CONNECTION_STATE_DISCONNECTED: disconnected
 * GST_WEBRTC_ICE_CONNECTION_STATE_CLOSED: closed
 *
 * See http://w3c.github.io/webrtc-pc/#dom-rtciceconnectionstate
 */
typedef enum /*< underscore_name=gst_webrtc_ice_connection_state >*/
{
  GST_WEBRTC_ICE_CONNECTION_STATE_NEW,
  GST_WEBRTC_ICE_CONNECTION_STATE_CHECKING,
  GST_WEBRTC_ICE_CONNECTION_STATE_CONNECTED,
  GST_WEBRTC_ICE_CONNECTION_STATE_COMPLETED,
  GST_WEBRTC_ICE_CONNECTION_STATE_FAILED,
  GST_WEBRTC_ICE_CONNECTION_STATE_DISCONNECTED,
  GST_WEBRTC_ICE_CONNECTION_STATE_CLOSED,
} GstWebRTCICEConnectionState;

/**
 * GstWebRTCSignalingState:
 * GST_WEBRTC_SIGNALING_STATE_STABLE: stable
 * GST_WEBRTC_SIGNALING_STATE_CLOSED: closed
 * GST_WEBRTC_SIGNALING_STATE_HAVE_LOCAL_OFFER: have-local-offer
 * GST_WEBRTC_SIGNALING_STATE_HAVE_REMOTE_OFFER: have-remote-offer
 * GST_WEBRTC_SIGNALING_STATE_HAVE_LOCAL_PRANSWER: have-local-pranswer
 * GST_WEBRTC_SIGNALING_STATE_HAVE_REMOTE_PRANSWER: have-remote-pranswer
 *
 * See http://w3c.github.io/webrtc-pc/#dom-rtcsignalingstate
 */
typedef enum /*< underscore_name=gst_webrtc_signaling_state >*/
{
  GST_WEBRTC_SIGNALING_STATE_STABLE,
  GST_WEBRTC_SIGNALING_STATE_CLOSED,
  GST_WEBRTC_SIGNALING_STATE_HAVE_LOCAL_OFFER,
  GST_WEBRTC_SIGNALING_STATE_HAVE_REMOTE_OFFER,
  GST_WEBRTC_SIGNALING_STATE_HAVE_LOCAL_PRANSWER,
  GST_WEBRTC_SIGNALING_STATE_HAVE_REMOTE_PRANSWER,
} GstWebRTCSignalingState;

/**
 * GstWebRTCPeerConnectionState:
 * GST_WEBRTC_PEER_CONNECTION_STATE_NEW: new
 * GST_WEBRTC_PEER_CONNECTION_STATE_CONNECTING: connecting
 * GST_WEBRTC_PEER_CONNECTION_STATE_CONNECTED: connected
 * GST_WEBRTC_PEER_CONNECTION_STATE_DISCONNECTED: disconnected
 * GST_WEBRTC_PEER_CONNECTION_STATE_FAILED: failed
 * GST_WEBRTC_PEER_CONNECTION_STATE_CLOSED: closed
 *
 * See http://w3c.github.io/webrtc-pc/#dom-rtcpeerconnectionstate
 */
typedef enum /*< underscore_name=gst_webrtc_peer_connection_state >*/
{
  GST_WEBRTC_PEER_CONNECTION_STATE_NEW,
  GST_WEBRTC_PEER_CONNECTION_STATE_CONNECTING,
  GST_WEBRTC_PEER_CONNECTION_STATE_CONNECTED,
  GST_WEBRTC_PEER_CONNECTION_STATE_DISCONNECTED,
  GST_WEBRTC_PEER_CONNECTION_STATE_FAILED,
  GST_WEBRTC_PEER_CONNECTION_STATE_CLOSED,
} GstWebRTCPeerConnectionState;

/**
 * GstWebRTCIceRole:
 * GST_WEBRTC_ICE_ROLE_CONTROLLED: controlled
 * GST_WEBRTC_ICE_ROLE_CONTROLLING: controlling
 */
typedef enum /*< underscore_name=gst_webrtc_ice_role >*/
{
  GST_WEBRTC_ICE_ROLE_CONTROLLED,
  GST_WEBRTC_ICE_ROLE_CONTROLLING,
} GstWebRTCIceRole;

/**
 * GstWebRTCIceComponent:
 * GST_WEBRTC_ICE_COMPONENT_RTP,
 * GST_WEBRTC_ICE_COMPONENT_RTCP,
 */
typedef enum /*< underscore_name=gst_webrtc_ice_component >*/
{
  GST_WEBRTC_ICE_COMPONENT_RTP,
  GST_WEBRTC_ICE_COMPONENT_RTCP,
} GstWebRTCICEComponent;

/**
 * GstWebRTCSDPType:
 * GST_WEBRTC_SDP_TYPE_OFFER: offer
 * GST_WEBRTC_SDP_TYPE_PRANSWER: pranswer
 * GST_WEBRTC_SDP_TYPE_ANSWER: answer
 * GST_WEBRTC_SDP_TYPE_ROLLBACK: rollback
 *
 * See http://w3c.github.io/webrtc-pc/#rtcsdptype
 */
typedef enum /*< underscore_name=gst_webrtc_sdp_type >*/
{
  GST_WEBRTC_SDP_TYPE_OFFER = 1,
  GST_WEBRTC_SDP_TYPE_PRANSWER,
  GST_WEBRTC_SDP_TYPE_ANSWER,
  GST_WEBRTC_SDP_TYPE_ROLLBACK,
} GstWebRTCSDPType;

/**
 * GstWebRTCRtpTransceiverDirection:
 * GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_NONE: none
 * GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_INACTIVE: inactive
 * GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_SENDONLY: sendonly
 * GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_RECVONLY: recvonly
 * GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_SENDRECV: sendrecv
 */
typedef enum /*< underscore_name=gst_webrtc_rtp_transceiver_direction >*/
{
  GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_NONE,
  GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_INACTIVE,
  GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_SENDONLY,
  GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_RECVONLY,
  GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_SENDRECV,
} GstWebRTCRTPTransceiverDirection;

/**
 * GstWebRTCDTLSSetup:
 * GST_WEBRTC_DTLS_SETUP_NONE: none
 * GST_WEBRTC_DTLS_SETUP_ACTPASS: actpass
 * GST_WEBRTC_DTLS_SETUP_ACTIVE: sendonly
 * GST_WEBRTC_DTLS_SETUP_PASSIVE: recvonly
 */
typedef enum /*< underscore_name=gst_webrtc_dtls_setup >*/
{
  GST_WEBRTC_DTLS_SETUP_NONE,
  GST_WEBRTC_DTLS_SETUP_ACTPASS,
  GST_WEBRTC_DTLS_SETUP_ACTIVE,
  GST_WEBRTC_DTLS_SETUP_PASSIVE,
} GstWebRTCDTLSSetup;

#endif /* __GST_WEBRTC_FWD_H__ */
