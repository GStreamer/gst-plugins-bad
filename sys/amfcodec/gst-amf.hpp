#pragma once
#include <memory>
#include "AMF/include/core/Factory.h"
#include "AMF/include/core/Trace.h"
#include "AMF/include/components/VideoEncoderVCE.h"
#include "amf-trace-writer.hpp"
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
			AMF::Instance()->GetTrace()->GetResultText(result)); \
		return FALSE;                                               \
	}
#define SET_AMF_VALUE(object, name, val)                         \
	result = object->SetProperty(name, val);                     \
	if (result != AMF_OK) {                                      \
		AMF_LOG_WARNING(                                         \
			"Failed to set %ls, error: %ls.", name,              \
			AMF::Instance()->GetTrace()->GetResultText(result)); \
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

struct EncoderCaps {
	struct NameValuePair {
		int32_t value;
		std::wstring name;
	};
	std::list<NameValuePair> rate_control_methods;
};

class AMF {
public:
	static void Initialize();
	static AMF *Instance();
	static void Finalize();

private: // Private Initializer & Finalizer
	AMF();
	~AMF();

public: // Remove all Copy operators
	AMF(AMF const &) = delete;
	void operator=(AMF const &) = delete;
#pragma endregion Singleton

public:
	amf::AMFFactory *GetFactory();
	amf::AMFTrace *GetTrace();
	uint64_t GetRuntimeVersion();
	void FillCaps();
	EncoderCaps GetH264Caps(int deviceNum) const;
	EncoderCaps GetHEVCCaps(int deviceNum) const;
	int defaultDeviceH264() const;
	int defaultDeviceHEVC() const;
	bool H264Available() const;
	bool HEVCAvailable() const;

private:
	/// AMF Values
	HMODULE amf_module;
	uint64_t amf_version;

	/// AMF Functions
	AMFQueryVersion_Fn amf_version_fun;
	AMFInit_Fn amf_init_fun;

	/// AMF Objects
	amf::AMFFactory *factory;
	amf::AMFTrace *trace;
	std::unique_ptr<GstAMFTraceWriter> trace_writer;
	std::map<int, EncoderCaps> h264_caps;
	std::map<int, EncoderCaps> hevc_caps;
};
