#include "gstamfh265enc.h"
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "gstamfh265enc.h"

#include <gst/gst.h>
#include <gst/base/base.h>
#include <gst/video/video.h>
#include <gst/video/gstvideoencoder.h>
#include <string.h>
#include "gstamf.hpp"
#include "AMF/include/components/VideoEncoderHEVC.h"
#include <thread>
#include <chrono>
#if defined(_WIN32)
    #include <gst/d3d11/gstd3d11memory.h>
    #include <gst/d3d11/gstd3d11bufferpool.h>
    #include <gst/d3d11/gstd3d11utils.h>
#endif

G_DEFINE_TYPE(GstAMFh265Enc, gst_amfh265enc, GST_TYPE_AMF_BASE_ENC);
GST_ELEMENT_REGISTER_DEFINE (amfh265enc, "amfh265enc", GST_RANK_SECONDARY,
            GST_TYPE_AMFH265ENC);
GST_DEBUG_CATEGORY_EXTERN(gst_amfench265_debug);
#define GST_CAT_DEFAULT gst_amfench265_debug

#define GST_TYPE_RATE_CONTROL (gst_amf_rate_control_get_type ())
static GType
gst_amf_rate_control_get_type (void)
{
    static GType rate_control_type = 0;

    static const GEnumValue rate_control[] = {
    {AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_CONSTANT_QP, 
        "Constant Quantization Parameter",
        "cqp"},
    {AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_CBR,
        "Constant bitrate",
        "cbr"},
    {AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_PEAK_CONSTRAINED_VBR,
        "Peak-Constrained Variable Bit Rate",
        "peak-constrainted-vbr"},
    {AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_LATENCY_CONSTRAINED_VBR,
        "Latency-Constrained Variable Bit Rate",
        "latency-constrainted-vbr"},
    {0, NULL, NULL},
    };

    if (!rate_control_type) {
        rate_control_type =
            g_enum_register_static ("GstAMFRateControlHEVC",
            rate_control);
    }
    return rate_control_type;
}

#define GST_TYPE_USAGE (gst_amf_usage_get_type ())
static GType
gst_amf_usage_get_type (void)
{
    static GType usage_type = 0;

    static const GEnumValue usage[] = {
    {AMF_VIDEO_ENCODER_HEVC_USAGE_TRANSCONDING, 
        "Usage - Transcoding",
        "transcoding"},
    {AMF_VIDEO_ENCODER_HEVC_USAGE_ULTRA_LOW_LATENCY,
        "Usage - Ultra low latency",
        "ultra-low-latency"},
    {AMF_VIDEO_ENCODER_HEVC_USAGE_LOW_LATENCY,
        "Usage - Low latency",
        "low-latency"},
    {AMF_VIDEO_ENCODER_HEVC_USAGE_WEBCAM,
        "Usage - Webcam",
        "webcam"},
    {0, NULL, NULL},
    };

    if (!usage_type) {
        usage_type =
            g_enum_register_static ("GstAMFUsageHEVC",
            usage);
    }
    return usage_type;
}

#define GST_TYPE_QUALITY_PRESET (gst_amf_quality_preset_get_type ())
static GType
gst_amf_quality_preset_get_type (void)
{
    static GType quality_preset_type = 0;

    static const GEnumValue quality_preset[] = {
    {AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET_BALANCED, 
        "Balansed",
        "balansed"},
    {AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET_SPEED,
        "Speed",
        "speed"},
    {AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET_QUALITY,
        "Quality",
        "quality"},
    {0, NULL, NULL},
    };

    if (!quality_preset_type) {
        quality_preset_type =
            g_enum_register_static ("GstAMFQualityPresetHEVC",
            quality_preset);
    }
    return quality_preset_type;
}

#define GST_TYPE_PROFILE (gst_amf_profile_get_type ())
static GType
gst_amf_profile_get_type (void)
{
    static GType profile_type = 0;

    static const GEnumValue profile[] = {
    {AMF_VIDEO_ENCODER_HEVC_PROFILE_MAIN,
        "Main",
        "main"},
    {0, NULL, NULL},
    };

    if (!profile_type) {
        profile_type =
            g_enum_register_static ("GstAMFProfileHEVC",
            profile);
    }
    return profile_type;
}

static void gst_amfh265enc_set_property(GObject* object,
    guint property_id, const GValue* value, GParamSpec* pspec);
static void gst_amfh265enc_get_property(GObject* object,
    guint property_id, GValue* value, GParamSpec* pspec);
static void gst_amfh265enc_finalize(GObject* object);
static gboolean gst_amfh265enc_set_format(GstVideoEncoder* encoder,
    GstVideoCodecState* state);
//static GstFlowReturn gst_amfh265enc_finish(GstVideoEncoder* encoder);
static gboolean gst_amfh265enc_propose_allocation(GstVideoEncoder* encoder,
    GstQuery* query);
static gboolean amfh265enc_element_init(GstPlugin* plugin);
static gboolean init_h265_encoder(GstVideoEncoder* encoder,
    GstVideoCodecFrame* frame);
static gboolean is_sync_point_h265(const amf::AMFBufferPtr& packetData);

static void
gst_amfh265enc_class_init(GstAMFh265EncClass* klass)
{
    GObjectClass* gobject_class = G_OBJECT_CLASS(klass);
    GstElementClass* element_class = GST_ELEMENT_CLASS(klass);
    GstVideoEncoderClass* video_encoder_class = GST_VIDEO_ENCODER_CLASS(klass);

    GstCaps* sink_caps = NULL;
    GstCaps* src_caps = NULL;

    sink_caps = gst_caps_from_string(
#if defined(_WIN32)
        "video/x-raw("
        GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY
        "), format = (string) NV12; "
#endif
        "video/x-raw, format = (string) NV12"
    );

    src_caps = gst_caps_from_string("video/x-h265"
        ", stream-format= (string) { avc, avc3, byte-stream }, "
        "alignment= (string) au, "
        "profile = (string) { high, progressive-high, constrained-high, main, constrained-baseline, baseline }");

    gst_element_class_add_pad_template(element_class,
        gst_pad_template_new("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
            sink_caps));
    gst_element_class_add_pad_template(element_class,
        gst_pad_template_new("src", GST_PAD_SRC, GST_PAD_ALWAYS,
            src_caps));

    gst_element_class_set_static_metadata(GST_ELEMENT_CLASS(klass),
        "AMF HEVC video encoder", "Encoder/Video", "AMF HEVC video encoder",
        "AMD AMF, https://github.com/GPUOpen-LibrariesAndSDKs/AMF");

    gobject_class->set_property = gst_amfh265enc_set_property;
    gobject_class->get_property = gst_amfh265enc_get_property;
    video_encoder_class->set_format =
        GST_DEBUG_FUNCPTR(gst_amfh265enc_set_format);
    video_encoder_class->propose_allocation =
        GST_DEBUG_FUNCPTR(gst_amfh265enc_propose_allocation);
    g_object_class_install_property(gobject_class, PROP_DEVICE_NUM,
        g_param_spec_int("device-num",
            "Device Number",
            "Set the GPU device to use for operations (-1 = auto)",
            -1, G_MAXINT, -1,
            (GParamFlags)(G_PARAM_READWRITE | GST_PARAM_MUTABLE_READY |
            G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property (gobject_class, PROP_RATE_CONTROL,
      g_param_spec_enum ("rate-control", "Rate control method",
          "Rate control method", GST_TYPE_RATE_CONTROL,
          AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_CBR,
          (GParamFlags) (G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE |
              G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property (gobject_class, PROP_USAGE,
      g_param_spec_enum ("usage", "Usage",
          "Usage", GST_TYPE_USAGE,
          AMF_VIDEO_ENCODER_USAGE_TRANSCONDING,
          (GParamFlags) (G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE |
              G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property (gobject_class, PROP_QUALITY_PRESET,
      g_param_spec_enum ("quality-preset", "Quality preset",
          "Quality preset", GST_TYPE_QUALITY_PRESET,
          AMF_VIDEO_ENCODER_QUALITY_PRESET_QUALITY,
          (GParamFlags) (G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE |
              G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property (gobject_class, PROP_PROFILE,
      g_param_spec_enum ("profile", "Encoder Profile",
          "Encoder Profile", GST_TYPE_PROFILE,
          AMF_VIDEO_ENCODER_PROFILE_HIGH,
          (GParamFlags) (G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE |
              G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property (gobject_class, PROP_LOW_LATENCY,
      g_param_spec_boolean ("enable-low-latency", "Low Latency mode",
          "Low Latency mode",
          FALSE, (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    
    g_object_class_install_property (gobject_class, PROP_PREENCODE,
      g_param_spec_boolean ("enable-pre-encode", "Pre-encode assisted rate control ",
          "Enables pre-encode assisted rate control ",
          FALSE, (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property (gobject_class, PROP_BUFFER_SIZE,
      g_param_spec_uint ("buffer-size", "VBV Buffer size",
          "VBV Buffer size(in seconds)",
          0, G_MAXUINT, 1,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              GST_PARAM_MUTABLE_PLAYING)));
    g_object_class_install_property (gobject_class, PROP_MOTION_BOOST,
      g_param_spec_boolean ("enable-motion-boost", "High motion quality boost",
          "High motion quality boost",
          FALSE, (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property (gobject_class, PROP_ENFORCE_HDR,
      g_param_spec_boolean ("enable-enforce-hdr", "Enforce HRD",
          "Enforce HRD",
          TRUE, (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property (gobject_class, PROP_KEYFRAME_INTERVAL,
      g_param_spec_uint ("keyframe-interval", "Keyframe interval",
          "Keyframe interval(in seconds)",
          0, G_MAXUINT, 2,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              GST_PARAM_MUTABLE_PLAYING)));
    g_object_class_install_property (gobject_class, PROP_DE_BLOCKING,
      g_param_spec_boolean ("enable-de-blocking", "De-blocking Filter",
          "De-blocking Filter",
          TRUE, (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    GST_DEBUG_CATEGORY_INIT (gst_amfench265_debug,
      "amfh265", 0, "AMF h265 encoder");
}

static void
gst_amfh265enc_init(GstAMFh265Enc* enc)
{
    enc->base_enc.init_encoder = init_h265_encoder;
    enc->base_enc.is_sync_point = is_sync_point_h265;
    enc->rate_control = AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_CBR;
    enc->usage = AMF_VIDEO_ENCODER_HEVC_USAGE_TRANSCONDING;
    enc->quality_preset = AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET_QUALITY;
    enc->profile = AMF_VIDEO_ENCODER_HEVC_PROFILE_MAIN;
    enc->low_latency_mode = false;
    enc->preencode_mode = false;
    enc->base_enc.bitrate = 6000;
    enc->base_enc.bitrate_peak = 9000;
    enc->buffer_size = 1;
    enc->motion_boost = false;
    enc->enforce_hdr = true;
    enc->keyframe_interval = 2;
    enc->de_blocking_filter = true;
    enc->base_enc.device_num = defaultDeviceHEVC(enc->base_enc.amf_ctx);
}

void
gst_amfh265enc_set_property(GObject* object, guint property_id,
    const GValue* value, GParamSpec* pspec)
{
    GstAMFh265Enc* amfh265enc = GST_AMFH265ENC(object);

    GST_DEBUG_OBJECT(amfh265enc, "set_property");
    switch (property_id) {
    case PROP_DEVICE_NUM:
        amfh265enc->base_enc.device_num = g_value_get_int(value);
        break;
    case PROP_RATE_CONTROL:
        amfh265enc->rate_control = (AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_ENUM)
            g_value_get_enum(value);
        break;
    case PROP_USAGE:
        amfh265enc->usage = (AMF_VIDEO_ENCODER_HEVC_USAGE_ENUM)
            g_value_get_enum(value);
        break;
    case PROP_QUALITY_PRESET:
        amfh265enc->quality_preset = (AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET_ENUM)
            g_value_get_enum(value);
        break;
    case PROP_PROFILE:
        amfh265enc->profile = (AMF_VIDEO_ENCODER_HEVC_PROFILE_ENUM)
            g_value_get_enum(value);
        break;
    case PROP_LOW_LATENCY:
        amfh265enc->low_latency_mode = g_value_get_boolean (value);
        break;
    case PROP_BUFFER_SIZE:
        amfh265enc->buffer_size = g_value_get_uint (value);
        break;
    case PROP_MOTION_BOOST:
        amfh265enc->motion_boost = g_value_get_boolean (value);
        break;
    case PROP_PREENCODE:
        amfh265enc->preencode_mode = g_value_get_boolean (value);
        break;
    case PROP_ENFORCE_HDR:
        amfh265enc->enforce_hdr = g_value_get_boolean (value);
        break;
    case PROP_KEYFRAME_INTERVAL:
        amfh265enc->keyframe_interval = g_value_get_uint (value);
        break;
    case PROP_DE_BLOCKING:
        amfh265enc->de_blocking_filter = g_value_get_boolean (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

void
gst_amfh265enc_get_property(GObject* object, guint property_id,
    GValue* value, GParamSpec* pspec)
{
    GstAMFh265Enc* enc = GST_AMFH265ENC(object);

    GST_DEBUG_OBJECT(enc, "get_property");
    switch (property_id) {
    case PROP_RATE_CONTROL:
        g_value_set_enum (value, enc->rate_control);
        break;
    case PROP_USAGE:
        g_value_set_enum (value, enc->usage);
        break;
    case PROP_QUALITY_PRESET:
        g_value_set_enum (value, enc->quality_preset);
        break;
    case PROP_PROFILE:
        g_value_set_enum (value, enc->profile);
        break;
    case PROP_LOW_LATENCY:
        g_value_set_boolean (value, enc->low_latency_mode);
        break;
    case PROP_PREENCODE:
        g_value_set_enum (value, enc->preencode_mode);
        break;
    case PROP_BUFFER_SIZE:
        g_value_set_uint (value, enc->buffer_size);
        break;
    case PROP_MOTION_BOOST:
        g_value_set_boolean (value, enc->motion_boost);
        break;
    case PROP_ENFORCE_HDR:
        g_value_set_boolean (value, enc->enforce_hdr);
        break;
    case PROP_KEYFRAME_INTERVAL:
        g_value_set_uint (value, enc->keyframe_interval);
        break;
    case PROP_DE_BLOCKING:
        g_value_set_boolean (value, enc->de_blocking_filter);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

static gboolean setup_encoder(GstAMFh265Enc* enc)
{
    GstAMFBaseEnc * base_enc = &enc->base_enc;
    guint fps_n, fps_d;
    AMF_RESULT result = AMF_FAIL;
    amf::AMFVariant p;
    
    fps_n = GST_VIDEO_INFO_FPS_N(&base_enc->in_state->info);
    fps_d = GST_VIDEO_INFO_FPS_D(&base_enc->in_state->info);

    base_enc->frameH = GST_VIDEO_INFO_HEIGHT(&base_enc->in_state->info);
    base_enc->frameW = GST_VIDEO_INFO_WIDTH(&base_enc->in_state->info);
    base_enc->frame_rate = AMFConstructRate(fps_n, fps_d);
    amf_double frameRateFraction =
        ((amf_double)fps_d / (amf_double)fps_n);
    base_enc->timestamp_step = AMF_SECOND * frameRateFraction;
    base_enc->query_wait_time = std::chrono::milliseconds(1);
    amf::AMFTrace * trace = GetTrace(base_enc->amf_ctx);
    
    SET_AMF_VALUE_OR_FAIL(base_enc->encoder_amf, AMF_VIDEO_ENCODER_HEVC_FRAMESIZE,
        AMFConstructSize(base_enc->frameW, base_enc->frameH));

    SET_AMF_VALUE_OR_FAIL(base_enc->encoder_amf, AMF_VIDEO_ENCODER_HEVC_USAGE,
        enc->usage);

    SET_AMF_VALUE_OR_FAIL(base_enc->encoder_amf,
        AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET,
        enc->quality_preset);

    SET_AMF_VALUE_OR_FAIL(base_enc->encoder_amf, 
        AMF_VIDEO_ENCODER_HEVC_PROFILE,
        enc->profile);

    SET_AMF_VALUE(base_enc->encoder_amf, AMF_VIDEO_ENCODER_HEVC_LOWLATENCY_MODE,
        enc->low_latency_mode);

    SET_AMF_VALUE(base_enc->encoder_amf, AMF_VIDEO_ENCODER_HEVC_PRE_ANALYSIS_ENABLE,
        enc->preencode_mode);

    SET_AMF_VALUE_OR_FAIL(base_enc->encoder_amf,
        AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD, 
        static_cast<uint64_t>(enc->rate_control));


    result = base_enc->encoder_amf->Init(amf::AMF_SURFACE_NV12, base_enc->frameW,
        base_enc->frameH);
    if (result != AMF_OK) {
        AMF_LOG_WARNING("AMF: Failed to init encoder");
        return FALSE;
    }
    SET_AMF_VALUE(base_enc->encoder_amf, AMF_VIDEO_ENCODER_HEVC_FRAMERATE,
        base_enc->frame_rate);

    AMF_RESULT res =
        base_enc->encoder_amf->GetProperty(AMF_VIDEO_ENCODER_HEVC_EXTRADATA, &p);
    if (res == AMF_OK && p.type == amf::AMF_VARIANT_INTERFACE) {
        base_enc->header = amf::AMFBufferPtr(p.pInterface);
    }

    ///dinamic properties
    SET_AMF_VALUE(base_enc->encoder_amf, AMF_VIDEO_ENCODER_HEVC_ENABLE_VBAQ,
        true);

    // Rate Control Properties
    int64_t bitrate = base_enc->bitrate * 1000;
    int64_t bitratePeak = base_enc->bitrate_peak * 1000;
    if (enc->rate_control != AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_CONSTANT_QP) {
        SET_AMF_VALUE_OR_FAIL(base_enc->encoder_amf,
            AMF_VIDEO_ENCODER_HEVC_TARGET_BITRATE,
            bitrate);

        SET_AMF_VALUE_OR_FAIL(
            base_enc->encoder_amf, AMF_VIDEO_ENCODER_HEVC_PEAK_BITRATE,
            bitratePeak);
    }
    SET_AMF_VALUE_OR_FAIL(base_enc->encoder_amf, AMF_VIDEO_ENCODER_HEVC_ENFORCE_HRD,
        enc->enforce_hdr);

    SET_AMF_VALUE(base_enc->encoder_amf,
        AMF_VIDEO_ENCODER_HEVC_HIGH_MOTION_QUALITY_BOOST_ENABLE,
        enc->motion_boost);

    // VBV Buffer
    SET_AMF_VALUE_OR_FAIL(
        base_enc->encoder_amf, AMF_VIDEO_ENCODER_HEVC_VBV_BUFFER_SIZE,
        static_cast<amf_int64>(
            bitrate * enc->buffer_size));

    // Picture Control
    int keyinterv = enc->keyframe_interval;
    int idrperiod = keyinterv * base_enc->frame_rate.num;
    SET_AMF_VALUE(base_enc->encoder_amf, AMF_VIDEO_ENCODER_HEVC_NUM_GOPS_PER_IDR,
		      1);
	SET_AMF_VALUE(base_enc->encoder_amf, AMF_VIDEO_ENCODER_HEVC_GOP_SIZE,
		      (int64_t)AMF_CLAMP(idrperiod, 1, 1000000));
    SET_AMF_VALUE(base_enc->encoder_amf, AMF_VIDEO_ENCODER_HEVC_DE_BLOCKING_FILTER_DISABLE,
        !enc->de_blocking_filter);
    GetTrace(base_enc->amf_ctx)->SetGlobalLevel(
        AMF_TRACE_ERROR);
    
    //This property reduces polling latency.
    SET_AMF_VALUE(base_enc->encoder_amf, L"TIMEOUT", 50);
    base_enc->initialised = true;
    return TRUE;
}

static gboolean init_h265_encoder(GstVideoEncoder* encoder,
    GstVideoCodecFrame* frame)
{
    GstAMFBaseEnc* enc = GST_AMF_BASE_ENC(encoder);
    if (enc->initialised)
        return TRUE;
    switch(enc->mem_type)
    {
#if defined(_WIN32)
        case GST_AMF_MEM_TYPE_D3D11:
        {
            if (frame == nullptr)
                return FALSE;
            for (int i = 0; i < gst_buffer_n_memory(frame->input_buffer); i++) 
            {
                GstD3D11Memory* mem = (GstD3D11Memory*)gst_buffer_peek_memory(frame->input_buffer, 0);
                
                if (!enc->initialised)
                {
                    AMF_RESULT res = AMF_FAIL;
                    ID3D11Device* handle = gst_d3d11_device_get_device_handle(mem->device);
                    
                    res = enc->context->InitDX11(handle, amf::AMF_DX11_1);
                    if (res != AMF_OK) {
                        GST_ERROR_OBJECT(enc, "Failed to init from dx11.");
                        return GST_FLOW_ERROR;
                    }
                    enc->device = (GstD3D11Device *)gst_object_ref(mem->device);
                    res = GetFactory(enc->amf_ctx)->CreateComponent(
                            enc->context, AMFVideoEncoder_HEVC,
                            &enc->encoder_amf);
                    if (res != AMF_OK) {
                        GST_ERROR_OBJECT(enc, "Failed to create hevc encoder.");
                        return GST_FLOW_ERROR;
                    }
                }
                break;
            }
            break;
        }
#endif
        case GST_AMF_MEM_TYPE_SYSTEM:
        {
            AMF_RESULT result = AMF_FAIL;
#if defined(_WIN32)
            result = init_d3d11(enc->device_num, enc);
            if (result != AMF_OK) {
                GST_ERROR_OBJECT(enc, "Failed to create d3d11 device.");
                return FALSE;
            }
            ID3D11Device* handle = gst_d3d11_device_get_device_handle(enc->device);
            result = enc->context->InitDX11(handle, amf::AMF_DX11_1);
#else
            result = amf::AMFContext1Ptr(enc->context)->InitVulkan(NULL);
#endif
            if (result != AMF_OK) {
                GST_ERROR_OBJECT(enc, "Failed to init from d3d11.");
                return FALSE;
            }

            // Create Encoder
            result = GetFactory(enc->amf_ctx)->CreateComponent(
                    enc->context, AMFVideoEncoder_HEVC,
                    &enc->encoder_amf);
            if (result != AMF_OK) {
                GST_ERROR_OBJECT(enc, "Failed to create h265 encoder.");
                return FALSE;
            }   
            break;
        }
    }
    GstAMFh265Enc* enc_265 = GST_AMFH265ENC(encoder);
    setup_encoder(enc_265);
    enc->initialised = true;
    return TRUE;
}

static gboolean
gst_amfh265enc_set_format(GstVideoEncoder* encoder,
    GstVideoCodecState* state)
{
    GstAMFh265Enc* enc = GST_AMFH265ENC(encoder);
    GST_DEBUG_OBJECT(enc, "set_format");
    enc->base_enc.in_state = state;
    GstVideoInfo info = state->info;
    GST_INFO_OBJECT (enc, "input caps: %" GST_PTR_FORMAT, state->caps);

    enc->base_enc.mem_type = GST_AMF_MEM_TYPE_SYSTEM;
#if defined(_WIN32)
    int n = gst_caps_get_size(state->caps);
    for (int i = 0; i < n; i++) {
        GstCapsFeatures* orig_features = gst_caps_get_features(state->caps, i);
        if (gst_caps_features_contains(orig_features, GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY)) {
            enc->base_enc.mem_type = GST_AMF_MEM_TYPE_D3D11;
            break;
        }
    }
#endif
    GstStructure *s;
    GstCaps *out_caps;
    GstVideoCodecState *output_state;

    out_caps = gst_caps_new_empty_simple ("video/x-h265");
    s = gst_caps_get_structure (out_caps, 0);
    gst_structure_set (s, "stream-format", G_TYPE_STRING, "byte-stream",
      "alignment", G_TYPE_STRING, "au", NULL);
    output_state = gst_video_encoder_set_output_state (GST_VIDEO_ENCODER (enc),
      out_caps, state);

    GST_INFO_OBJECT (enc, "output caps: %" GST_PTR_FORMAT, output_state->caps);
    gst_video_codec_state_unref (output_state);
    gst_amf_enc_set_latency(GST_AMF_BASE_ENC(encoder));
    return TRUE;
}

static gboolean
gst_amfh265enc_propose_allocation(GstVideoEncoder* encoder, GstQuery* query)
{
    gst_query_add_allocation_meta(query, GST_VIDEO_META_API_TYPE, NULL);

    return TRUE;
}

static gboolean is_sync_point_h265(const amf::AMFBufferPtr& packetData)
{
    uint64_t pktType;
    packetData->GetProperty(AMF_VIDEO_ENCODER_HEVC_OUTPUT_DATA_TYPE, &pktType);
    if (pktType == AMF_VIDEO_ENCODER_HEVC_OUTPUT_DATA_TYPE_IDR) 
        return TRUE;
    else 
        return FALSE;
}