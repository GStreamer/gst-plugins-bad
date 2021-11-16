#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <gst/gst.h>
#include "gstamf.hpp"
#include "gstamfh264enc.h"
#include "gstamfh265enc.h"

GST_DEBUG_CATEGORY (gst_amfenc_debug);
GST_DEBUG_CATEGORY (gst_amfench264_debug);
GST_DEBUG_CATEGORY (gst_amfench265_debug);

static gboolean
plugin_init (GstPlugin * plugin)
{
  gboolean ret = TRUE;
  if (gst_amf_h264_available ()) {
    ret |= GST_ELEMENT_REGISTER (amfh264enc, plugin);
  }
  if (gst_amf_h265_available ()) {
    ret |= GST_ELEMENT_REGISTER (amfh265enc, plugin);
  }

  return ret;
}

#ifndef PACKAGE
#define PACKAGE "amfcodec"
#endif

#ifndef VERSION
#define VERSION "1.0.0.0"
#endif

#ifndef GST_PACKAGE_ORIGIN
#define GST_PACKAGE_ORIGIN "amfcodec"
#endif

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    amfcodec,
    "AMF encoder/decoder plugin",
    plugin_init, VERSION, "BSD", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
