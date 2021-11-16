#pragma once
#include <gst/video/video.h>
#include <gst/video/gstvideoencoder.h>
#include "AMF/include/core/Factory.h"
#if defined(_WIN32)
	#include <gst/d3d11/gstd3d11device.h>
	#include <d3d11.h>
	#include <d3d11_1.h>
	#include <atlbase.h>
#endif
#include <gst/gst.h>
#include <chrono>
#include "AMF/include/components/VideoEncoderHEVC.h"
#include "gstamf.hpp"
#include "gstamfcontext.hpp"

G_BEGIN_DECLS

#define GST_TYPE_AMF_BASE_ENC          (gst_amf_base_enc_get_type())
#define GST_AMF_BASE_ENC(obj)          (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_AMF_BASE_ENC, GstAMFBaseEnc))
#define GST_AMF_BASE_ENC_CLASS(klass)  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_AMF_BASE_ENC, GstAMFBaseEncClass))
#define GST_IS_AMF_BASE_ENC(obj)       (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_AMF_BASE_ENC))
#define GST_IS_AMF_BASE_ENC_CLASS(obj) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_AMF_BASE_ENC))

typedef struct _GstAMFBaseEnc GstAMFBaseEnc;
typedef struct _GstAMFBaseEncClass GstAMFBaseEncClass;

struct _GstAMFBaseEnc
{
	GstVideoEncoder base_enc;

	//
	GstVideoCodecState* in_state;
	GstAMFMemType mem_type;
#if defined(_WIN32)
	GstD3D11Device *device;
#endif
    GstAMFContext * amf_ctx;
	amf::AMFContextPtr context;
	amf::AMFComponentPtr encoder_amf;
	gboolean (*init_encoder) (GstVideoEncoder* encoder,
    GstVideoCodecFrame* frame);
	gboolean (*is_sync_point) (const amf::AMFBufferPtr& packetData);
	
	int frameW;
	int frameH;
	AMFRate frame_rate;
	amf_double timestamp_step;
	std::chrono::nanoseconds query_wait_time;
	amf::AMFBufferPtr header;
	GThread *processing_thread;

	bool initialised;
    //properties
	int 										device_num;
	int64_t										bitrate;
	int64_t 									bitrate_peak;

	GAsyncQueue       *pending_queue;
};

struct _GstAMFBaseEncClass
{
    GstVideoEncoderClass video_encoder_class;
};

GType gst_amf_base_enc_get_type(void);
#if defined(_WIN32)
AMF_RESULT init_d3d11(uint32_t adapterIndex, GstAMFBaseEnc* enc);
#endif
void gst_amf_enc_set_latency (GstAMFBaseEnc* amf_base_enc);
G_END_DECLS