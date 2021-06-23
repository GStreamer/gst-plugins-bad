#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <gst/gst.h>
#include "gst-amf.hpp"
#include "gstamfh264enc.h"
#include "gstamfh265enc.h"

GST_DEBUG_CATEGORY (gst_amfenc_debug);

static gboolean 
plugin_init(GstPlugin* plugin)
{
    gboolean ret = TRUE;
    AMF::Initialize();
    AMF::Instance()->FillCaps();
    if (AMF::Instance()->H264Available())
    {
        gst_element_register(plugin, "amfh264enc", GST_RANK_NONE,
            GST_TYPE_AMFH264ENC);
    }
    if (AMF::Instance()->HEVCAvailable())
    {
        gst_element_register(plugin, "amfh265enc", GST_RANK_NONE,
            GST_TYPE_AMFH265ENC);
    }

    return ret;
}
#ifndef PACKAGE
#define PACKAGE "amfcodec"
#endif

#ifndef VERSION
#define VERSION "1.0.0.0"
#endif

#ifndef GST_PACKAGE_NAME
#define GST_PACKAGE_NAME "amfcodec"
#endif

#ifndef GST_PACKAGE_ORIGIN
#define GST_PACKAGE_ORIGIN "amfcodec"
#endif

GST_PLUGIN_DEFINE(GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    amfcodec,
    "AMF encoder/decoder plugin",
    plugin_init, VERSION, "BSD", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
