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
#include "AMF/include/components/VideoEncoderVCE.h"
#include "gst-amf.hpp"

G_BEGIN_DECLS

#define GST_TYPE_AMFH264ENC           (gst_amfh264enc_get_type())
#define GST_AMFH264ENC(obj)          (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_AMFH264ENC,GstAMFh264Enc))
#define GST_AMFH264ENC_CLASS(klass)  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_AMFH264ENC,GstAMFh264EncClass))
#define GST_IS_AMFH264ENC(obj)       (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_AMFH264ENC))
#define GST_IS_AMFH264ENC_CLASS(obj) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_AMFH264ENC))

typedef struct _GstAMFh264Enc GstAMFh264Enc;
typedef struct _GstAMFh264EncClass GstAMFh264EncClass;


struct _GstAMFh264Enc
{
    GstVideoEncoder base_openh264enc;

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
	int 										device_num;
	AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_ENUM 	rate_control;
	AMF_VIDEO_ENCODER_USAGE_ENUM 				usage;
	AMF_VIDEO_ENCODER_QUALITY_PRESET_ENUM 		quality_preset;
	AMF_VIDEO_ENCODER_PROFILE_ENUM 				profile;
	bool 										low_latency_mode;
	AMF_VIDEO_ENCODER_PREENCODE_MODE_ENUM		preencode;
	AMF_VIDEO_ENCODER_CODING_ENUM				coding_type;
	int64_t										bitrate;
	int64_t 									bitrate_peak;
	int											buffer_size;//seconds
	bool 										motion_boost;
	bool 										enforce_hdr;
	int											keyframe_interval;//seconds
	bool 										de_blocking_filter;
};

struct _GstAMFh264EncClass
{
    GstVideoEncoderClass base_amf264enc_class;
};


GType gst_amfh264enc_get_type(void);
G_END_DECLS