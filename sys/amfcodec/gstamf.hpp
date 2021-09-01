#pragma once
#include <memory>
#include "AMF/include/core/Factory.h"
#include "AMF/include/core/Trace.h"
#include "amftracewriter.hpp"
#include <map>
#include <list>
#include <memory>
#include <gst/gst.h>
extern "C" {
#if defined(WIN32) || defined(WIN64)
#include <windows.h>
#endif
}
GST_DEBUG_CATEGORY_EXTERN(gst_amfenc_debug);
#define GST_CAT_DEFAULT gst_amfenc_debug

#define AMF_LOG( ...) GST_INFO("[AMF] " __VA_ARGS__)
#define AMF_LOG_ERROR(...) GST_ERROR(__VA_ARGS__)
#define AMF_LOG_WARNING(...) GST_WARNING(__VA_ARGS__)
#define AMF_CLAMP(val, low, high) (val > high ? high : (val < low ? low : val))

#define SET_AMF_VALUE_OR_FAIL(object, name, val)                 \
	result = object->SetProperty(name, val);                     \
	if (result != AMF_OK) {                                      \
		AMF_LOG_WARNING(                                         \
			"Failed to set %ls, error: %ls.", name,              \
			trace->GetResultText(result)); \
		return FALSE;                                               \
	}
#define SET_AMF_VALUE(object, name, val)                         \
	result = object->SetProperty(name, val);                     \
	if (result != AMF_OK) {                                      \
		AMF_LOG_WARNING(                                         \
			"Failed to set %ls, error: %ls.", name,              \
			trace->GetResultText(result)); \
	}

#define AMF_PRESENT_TIMESTAMP L"PTS"

typedef enum
{
	GST_AMF_MEM_TYPE_SYSTEM = 0,
	GST_AMF_MEM_TYPE_D3D11,
} GstAMFMemType;


enum
{
	PROP_0,
	PROP_DEVICE_NUM,
	PROP_RATE_CONTROL,
	PROP_USAGE,
	PROP_QUALITY_PRESET,
	PROP_PROFILE,
	PROP_LOW_LATENCY,
	PROP_PREENCODE,
	PROP_CODING_TYPE,//only h264
	PROP_BITRATE,
	PROP_BITRATE_PEAK,
	PROP_BUFFER_SIZE,
	PROP_MOTION_BOOST,
	PROP_ENFORCE_HDR,
	PROP_KEYFRAME_INTERVAL,
	PROP_DE_BLOCKING
};

bool gst_amf_h264_available();
bool gst_amf_h265_available();