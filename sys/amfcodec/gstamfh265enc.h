#pragma once
#include <gst/video/video.h>
#include <gst/video/gstvideoencoder.h>
#include "AMF/include/core/Factory.h"
#include <gst/d3d11/gstd3d11device.h>
#include <d3d11.h>
#include <d3d11_1.h>
#include <atlbase.h>
#include <gst/gst.h>
#include <chrono>
#include "AMF/include/components/VideoEncoderHEVC.h"
#include "gst-amf.hpp"

G_BEGIN_DECLS

#define GST_TYPE_AMFH265ENC           (gst_amfh265enc_get_type())
#define GST_AMFH265ENC(obj)          (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_AMFH265ENC,GstAMFh265Enc))
#define GST_AMFH265ENC_CLASS(klass)  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_AMFH265ENC,GstAMFh265EncClass))
#define GST_IS_AMFH265ENC(obj)       (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_AMFH265ENC))
#define GST_IS_AMFH265ENC_CLASS(obj) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_AMFH265ENC))

typedef struct _GstAMFh265Enc GstAMFh265Enc;
typedef struct _GstAMFh265EncClass GstAMFh265EncClass;

struct _GstAMFh265Enc
{
    GstVideoEncoder base_openh265enc;

	//
	GstVideoCodecState* in_state;
	GstAMFMemType mem_type;
	GstD3D11Device *device;

    /*< private >*/
	amf::AMFContextPtr context;
	amf::AMFComponentPtr encoder_amf;

	
	int frameW;
	int frameH;
	AMFRate frame_rate;
	double_t timestamp_step;
	std::chrono::nanoseconds query_wait_time;
	amf::AMFBufferPtr header;

	bool initialised;

//properties
	int 											device_num;
	AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_ENUM rate_control;
	AMF_VIDEO_ENCODER_HEVC_USAGE_ENUM 				usage;
	AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET_ENUM 		quality_preset;
	AMF_VIDEO_ENCODER_HEVC_PROFILE_ENUM 			profile;
	bool 											low_latency_mode;

	int64_t											bitrate;
	int64_t 										bitrate_peak;
	int												buffer_size;//seconds
	bool 											motion_boost;
	bool 											enforce_hdr;
	bool 											preencode_mode;
	int												keyframe_interval;//seconds
	bool 											de_blocking_filter;
};

struct _GstAMFh265EncClass
{
    GstVideoEncoderClass base_amf265enc_class;
};

GType gst_amfh265enc_get_type(void);
G_END_DECLS