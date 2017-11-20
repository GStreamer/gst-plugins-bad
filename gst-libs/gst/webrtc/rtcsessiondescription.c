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

#include "rtcsessiondescription.h"

#define GST_CAT_DEFAULT gst_webrtc_peerconnection_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

const gchar *
gst_webrtc_sdp_type_to_string (GstWebRTCSDPType type)
{
  switch (type) {
    case GST_WEBRTC_SDP_TYPE_OFFER:
      return "offer";
    case GST_WEBRTC_SDP_TYPE_PRANSWER:
      return "pranswer";
    case GST_WEBRTC_SDP_TYPE_ANSWER:
      return "answer";
    case GST_WEBRTC_SDP_TYPE_ROLLBACK:
      return "rollback";
    default:
      return "unknown";
  }
}

GstWebRTCSessionDescription *
gst_webrtc_session_description_copy (const GstWebRTCSessionDescription * src)
{
  GstWebRTCSessionDescription *ret;

  if (!src)
    return NULL;

  ret = g_new0 (GstWebRTCSessionDescription, 1);

  ret->type = src->type;
  gst_sdp_message_copy (src->sdp, &ret->sdp);

  return ret;
}

void
gst_webrtc_session_description_free (GstWebRTCSessionDescription * desc)
{
  g_return_if_fail (desc != NULL);

  gst_sdp_message_free (desc->sdp);
  g_free (desc);
}

GstWebRTCSessionDescription *
gst_webrtc_session_description_new (GstWebRTCSDPType type, GstSDPMessage * sdp)
{
  GstWebRTCSessionDescription *ret;

  ret = g_new0 (GstWebRTCSessionDescription, 1);

  ret->type = type;
  ret->sdp = sdp;

  return ret;
}

G_DEFINE_BOXED_TYPE_WITH_CODE (GstWebRTCSessionDescription,
    gst_webrtc_session_description, gst_webrtc_session_description_copy,
    gst_webrtc_session_description_free,
    GST_DEBUG_CATEGORY_INIT (gst_webrtc_peerconnection_debug,
        "webrtcsessiondescription", 0, "webrtcsessiondescription"));
