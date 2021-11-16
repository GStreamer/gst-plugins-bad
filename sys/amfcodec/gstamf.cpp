#include "gstamf.hpp"
#include "AMF/include/components/VideoEncoderVCE.h"
#include "AMF/include/components/VideoEncoderHEVC.h"
#include <gmodule.h>

AMF_RESULT
check_component_available (const wchar_t * componentID)
{
  GModule *module;
  AMFInit_Fn initFun;
  amf::AMFFactory * factory = NULL;
  amf::AMFContextPtr context = NULL;
  amf::AMFComponentPtr encoder = NULL;
  AMF_RESULT result = AMF_FAIL;

  module = g_module_open (AMF_DLL_NAMEA, G_MODULE_BIND_LAZY);

  if (!module) {
    result = AMF_FAIL;
    AMF_LOG_ERROR ("Open AMF dll Failed");
    goto clean;
  }

  if (!g_module_symbol (module, AMF_INIT_FUNCTION_NAME, (gpointer *) & initFun)) {
    result = AMF_FAIL;
    AMF_LOG_ERROR ("Load Library Failed");
    goto clean;
  }

  result = initFun (AMF_FULL_VERSION, &factory);
  if (result != AMF_OK) {
    AMF_LOG_ERROR ("Init Failed");
    goto clean;
  }

  result = factory->CreateContext (&context);
  if (result != AMF_OK) {
    AMF_LOG_ERROR ("Context Failed");
    goto clean;
  }
#if defined(_WIN32)
  result = context->InitDX11 (NULL);
#else
  result = amf::AMFContext1Ptr (context)->InitVulkan (NULL);
#endif
  if (result != AMF_OK) {
    AMF_LOG_ERROR ("CreateContext1() failed");
    goto clean;
  }

  result = factory->CreateComponent (context, componentID, &encoder);

  if (result != AMF_OK) {
    goto clean;
  }
clean:
  factory = NULL;
  context = NULL;
  encoder = NULL;
  if (module) {
    g_module_close (module);
  }

  return result;
}

bool
gst_amf_h264_available ()
{
  return check_component_available (AMFVideoEncoderVCE_AVC) == AMF_OK;
}

bool
gst_amf_h265_available ()
{
  return check_component_available (AMFVideoEncoder_HEVC) == AMF_OK;
}
