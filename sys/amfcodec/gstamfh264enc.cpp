#include "gstamfh264enc.h"
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "gstamfh264enc.h"

#include <gst/gst.h>
#include <gst/base/base.h>
#include <gst/video/video.h>
#include <gst/video/gstvideoencoder.h>
#include <string.h>
#include "gst-amf.hpp"
#include "AMF/include/components/VideoEncoderVCE.h"
#include <thread>
#include <chrono>

#include <gst/d3d11/gstd3d11memory.h>
#include <gst/d3d11/gstd3d11bufferpool.h>
#include <gst/d3d11/gstd3d11utils.h>

GST_DEBUG_CATEGORY_EXTERN(gst_amfenc_debug);
#define GST_CAT_DEFAULT gst_amfenc_debug
G_DEFINE_TYPE(GstAMFh264Enc, gst_amfh264enc, GST_TYPE_VIDEO_ENCODER);

#define GST_TYPE_RATE_CONTROL (gst_amf_rate_control_get_type ())
static GType
gst_amf_rate_control_get_type (void)
{
    static GType rate_control_type = 0;

    static const GEnumValue rate_control[] = {
    {AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_CONSTANT_QP, "Constant Quantization Parameter",
        "cqp"},
    {AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_CBR,
        "Constant bitrate",
        "cbr"},
    {AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_PEAK_CONSTRAINED_VBR,
        "Peak-Constrained Variable Bit Rate",
        "peak-constrainted-vbr"},
    {AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_LATENCY_CONSTRAINED_VBR,
        "Latency-Constrained Variable Bit Rate",
        "latency-constrainted-vbr"},
    {AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_QUALITY_VBR,
        "Quality VBR",
        "quality-vbr"},
    {0, NULL, NULL},
    };

    if (!rate_control_type) {
        rate_control_type =
            g_enum_register_static ("GstAMFRateControlH264",
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
    {AMF_VIDEO_ENCODER_USAGE_TRANSCONDING, 
        "Usage - Transcoding",
        "transcoding"},
    {AMF_VIDEO_ENCODER_USAGE_ULTRA_LOW_LATENCY,
        "Usage - Ultra low latency",
        "ultra-low-latency"},
    {AMF_VIDEO_ENCODER_USAGE_LOW_LATENCY,
        "Usage - Low latency",
        "low-latency"},
    {AMF_VIDEO_ENCODER_USAGE_WEBCAM,
        "Usage - Webcam",
        "webcam"},
    {0, NULL, NULL},
    };

    if (!usage_type) {
        usage_type =
            g_enum_register_static ("GstAMFUsageH264",
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
    {AMF_VIDEO_ENCODER_QUALITY_PRESET_BALANCED, 
        "Balansed",
        "balansed"},
    {AMF_VIDEO_ENCODER_QUALITY_PRESET_SPEED,
        "Speed",
        "speed"},
    {AMF_VIDEO_ENCODER_QUALITY_PRESET_QUALITY,
        "Quality",
        "quality"},
    {0, NULL, NULL},
    };

    if (!quality_preset_type) {
        quality_preset_type =
            g_enum_register_static ("GstAMFQualityPresetH264",
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
    {AMF_VIDEO_ENCODER_PROFILE_BASELINE, 
        "Baseline",
        "baseline"},
    {AMF_VIDEO_ENCODER_PROFILE_MAIN,
        "Main",
        "main"},
    {AMF_VIDEO_ENCODER_PROFILE_HIGH,
        "High",
        "high"},
    {AMF_VIDEO_ENCODER_PROFILE_CONSTRAINED_BASELINE,
        "Constrainted Baseline",
        "constrainted-baseline"},
    {AMF_VIDEO_ENCODER_PROFILE_CONSTRAINED_HIGH,
        "Constrainted High",
        "constrainted-high"},
    {0, NULL, NULL},
    };

    if (!profile_type) {
        profile_type =
            g_enum_register_static ("GstAMFProfileH264",
            profile);
    }
    return profile_type;
}

#define GST_TYPE_PREENCODE (gst_amf_preencode_get_type ())
static GType
gst_amf_preencode_get_type (void)
{
    static GType preencode_type = 0;

    static const GEnumValue preencode[] = {
    {AMF_VIDEO_ENCODER_PREENCODE_DISABLED, 
        "Disabled",
        "disabled"},
    {AMF_VIDEO_ENCODER_PREENCODE_ENABLED,
        "Enabled",
        "enabled"},
    {0, NULL, NULL},
    };

    if (!preencode_type) {
        preencode_type =
            g_enum_register_static ("GstAMFPreencodeH264",
            preencode);
    }
    return preencode_type;
}

#define GST_TYPE_CODING_TYPE (gst_amf_coding_type_get_type ())
static GType
gst_amf_coding_type_get_type (void)
{
    static GType coding_type_type = 0;

    static const GEnumValue coding_type[] = {
    {AMF_VIDEO_ENCODER_UNDEFINED, 
        "Auto - BASELINE = CALV; MAIN, HIGH = CABAC ",
        "auto"},
    {AMF_VIDEO_ENCODER_CABAC,
        "CABAC",
        "cabac"},
    {AMF_VIDEO_ENCODER_CALV,
        "CALV",
        "calv"},
    {0, NULL, NULL},
    };

    if (!coding_type_type) {
        coding_type_type =
            g_enum_register_static ("GstAMFCodingTypeH264",
            coding_type);
    }
    return coding_type_type;
}

static void gst_amfh264enc_set_property(GObject* object,
    guint property_id, const GValue* value, GParamSpec* pspec);
static void gst_amfh264enc_get_property(GObject* object,
    guint property_id, GValue* value, GParamSpec* pspec);
static void gst_amfh264enc_finalize(GObject* object);
static gboolean gst_amfh264enc_start(GstVideoEncoder* encoder);
static gboolean gst_amfh264enc_stop(GstVideoEncoder* encoder);
static gboolean gst_amfh264enc_set_format(GstVideoEncoder* encoder,
    GstVideoCodecState* state);
static GstFlowReturn gst_amfh264enc_handle_frame(GstVideoEncoder* encoder,
    GstVideoCodecFrame* frame);
static GstFlowReturn gst_amfh264enc_finish(GstVideoEncoder* encoder);
static gboolean gst_amfh264enc_propose_allocation(GstVideoEncoder* encoder,
    GstQuery* query);
static gboolean amfh264enc_element_init(GstPlugin* plugin);

static void
gst_amfh264enc_class_init(GstAMFh264EncClass* klass)
{
    GObjectClass* gobject_class = G_OBJECT_CLASS(klass);
    GstElementClass* element_class = GST_ELEMENT_CLASS(klass);
    GstVideoEncoderClass* video_encoder_class = GST_VIDEO_ENCODER_CLASS(klass);

    GstCaps* sink_caps = NULL;
    GstCaps* src_caps = NULL;

    sink_caps = gst_caps_from_string("video/x-raw("
        GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY
        "), format = (string) NV12; "
        "video/x-raw, format = (string) NV12"
    );

    src_caps = gst_caps_from_string("video/x-h264"
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
        "AMF H264 video encoder", "Encoder/Video", "AMF H264 video encoder",
        "AMD AMF, https://github.com/GPUOpen-LibrariesAndSDKs/AMF");

    gobject_class->set_property = gst_amfh264enc_set_property;
    gobject_class->get_property = gst_amfh264enc_get_property;
    gobject_class->finalize = gst_amfh264enc_finalize;
    video_encoder_class->start = GST_DEBUG_FUNCPTR(gst_amfh264enc_start);
    video_encoder_class->stop = GST_DEBUG_FUNCPTR(gst_amfh264enc_stop);
    video_encoder_class->set_format =
        GST_DEBUG_FUNCPTR(gst_amfh264enc_set_format);
    video_encoder_class->handle_frame =
        GST_DEBUG_FUNCPTR(gst_amfh264enc_handle_frame);
    video_encoder_class->propose_allocation =
        GST_DEBUG_FUNCPTR(gst_amfh264enc_propose_allocation);
    video_encoder_class->finish = GST_DEBUG_FUNCPTR(gst_amfh264enc_finish);

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
      g_param_spec_enum ("preencode", "Preencode mode",
          "Preencode mode", GST_TYPE_PREENCODE,
          AMF_VIDEO_ENCODER_PREENCODE_ENABLED,
          (GParamFlags) (G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE |
              G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property (gobject_class, PROP_CODING_TYPE,
      g_param_spec_enum ("coding-type", "Coding type",
          "Coding type", GST_TYPE_CODING_TYPE,
          AMF_VIDEO_ENCODER_UNDEFINED,
          (GParamFlags) (G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE |
              G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property (gobject_class, PROP_BITRATE,
      g_param_spec_uint ("bitrate", "Bitrate",
          "Bitrate (in kbits per second)",
          0, G_MAXUINT, 6000,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              GST_PARAM_MUTABLE_PLAYING)));
    g_object_class_install_property (gobject_class, PROP_BITRATE,
      g_param_spec_uint ("bitrate-max", "Bitrate max",
          "Bitrate max(in kbits per second)",
          0, G_MAXUINT, 9000,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              GST_PARAM_MUTABLE_PLAYING)));
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
}

AMF_RESULT init_d3d11(uint32_t adapterIndex, GstAMFh264Enc* enc)
{
    enc->device = gst_d3d11_device_new (adapterIndex, D3D11_CREATE_DEVICE_BGRA_SUPPORT);
    if (!enc->device)
      return AMF_FAIL;
    guint device_id = 0;
    guint vendor_id = 0;
    gchar *desc = NULL;
    g_object_get (enc->device, "device-id", &device_id, "vendor-id", &vendor_id,
      "description", &desc, NULL);
    if (vendor_id != 0x1002) {
        AMF_LOG_ERROR("D3D11CreateDevice failed. Invalid vendor.");
        gst_object_unref (enc->device);
        return AMF_FAIL;
    }

    return AMF_OK;
}

static void
gst_amfh264enc_init(GstAMFh264Enc* enc)
{
    enc->rate_control = AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_CBR;
    enc->usage = AMF_VIDEO_ENCODER_USAGE_TRANSCONDING;
    enc->quality_preset = AMF_VIDEO_ENCODER_QUALITY_PRESET_QUALITY;
    enc->profile = AMF_VIDEO_ENCODER_PROFILE_HIGH;
    enc->low_latency_mode = false;
    enc->preencode = AMF_VIDEO_ENCODER_PREENCODE_ENABLED;
    enc->coding_type = AMF_VIDEO_ENCODER_UNDEFINED;
    enc->bitrate = 6000;
    enc->bitrate_peak = 9000;
    enc->buffer_size = 1;
    enc->motion_boost = false;
    enc->enforce_hdr = true;
    enc->keyframe_interval = 2;
    enc->de_blocking_filter = true;
    enc->device_num = AMF::Instance()->defaultDeviceH264();
    AMF_RESULT result = AMF_FAIL;
    result = AMF::Instance()->GetFactory()->CreateContext(&enc->context);
    if (result != AMF_OK) {
        AMF_LOG_WARNING("CreateContext Failed");
        return;
    }
}

void
gst_amfh264enc_set_property(GObject* object, guint property_id,
    const GValue* value, GParamSpec* pspec)
{
    GstAMFh264Enc* amfh264enc = GST_AMFH264ENC(object);

    GST_DEBUG_OBJECT(amfh264enc, "set_property");
    switch (property_id) {
    case PROP_DEVICE_NUM:
        amfh264enc->device_num = g_value_get_int(value);
        break;
    case PROP_RATE_CONTROL:
        amfh264enc->rate_control = (AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_ENUM)
            g_value_get_enum(value);
        break;
    case PROP_USAGE:
        amfh264enc->usage = (AMF_VIDEO_ENCODER_USAGE_ENUM)
            g_value_get_enum(value);
        break;
    case PROP_QUALITY_PRESET:
        amfh264enc->quality_preset = (AMF_VIDEO_ENCODER_QUALITY_PRESET_ENUM)
            g_value_get_enum(value);
        break;
    case PROP_PROFILE:
        amfh264enc->profile = (AMF_VIDEO_ENCODER_PROFILE_ENUM)
            g_value_get_enum(value);
        break;
    case PROP_LOW_LATENCY:
        amfh264enc->low_latency_mode = g_value_get_boolean (value);
        break;
    case PROP_PREENCODE:
        amfh264enc->preencode = (AMF_VIDEO_ENCODER_PREENCODE_MODE_ENUM)
            g_value_get_enum(value);
        break;
    case PROP_CODING_TYPE:
        amfh264enc->coding_type = (AMF_VIDEO_ENCODER_CODING_ENUM)
            g_value_get_enum(value);
        break;
    case PROP_BITRATE:
        amfh264enc->bitrate = g_value_get_uint (value);
        break;
    case PROP_BITRATE_PEAK:
        amfh264enc->bitrate_peak = g_value_get_uint (value);
        break;
    case PROP_BUFFER_SIZE:
        amfh264enc->buffer_size = g_value_get_uint (value);
        break;
    case PROP_MOTION_BOOST:
        amfh264enc->motion_boost = g_value_get_boolean (value);
        break;
    case PROP_ENFORCE_HDR:
        amfh264enc->enforce_hdr = g_value_get_boolean (value);
        break;
    case PROP_KEYFRAME_INTERVAL:
        amfh264enc->keyframe_interval = g_value_get_uint (value);
        break;
    case PROP_DE_BLOCKING:
        amfh264enc->de_blocking_filter = g_value_get_boolean (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

void
gst_amfh264enc_get_property(GObject* object, guint property_id,
    GValue* value, GParamSpec* pspec)
{
    GstAMFh264Enc* enc = GST_AMFH264ENC(object);

    GST_DEBUG_OBJECT(enc, "get_property");
    switch (property_id) {
    case PROP_DEVICE_NUM:
        g_value_set_int(value, enc->device_num);
        break;
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
        g_value_set_enum (value, enc->preencode);
        break;
    case PROP_CODING_TYPE:
        g_value_set_enum (value, enc->coding_type);
        break;
    case PROP_BITRATE:
        g_value_set_uint (value, enc->bitrate);
        break;
    case PROP_BITRATE_PEAK:
        g_value_set_uint (value, enc->bitrate_peak);
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

void
gst_amfh264enc_finalize(GObject* object)
{
    GstAMFh264Enc* enc = GST_AMFH264ENC(object);

    GST_DEBUG_OBJECT(enc, "finalize");
}

static gboolean
gst_amfh264enc_start(GstVideoEncoder* encoder)
{
    GstAMFh264Enc* enc = GST_AMFH264ENC(encoder);
    GST_DEBUG_OBJECT(enc, "start");

    return TRUE;
}

static gboolean
gst_amfh264enc_stop(GstVideoEncoder* encoder)
{
    GstAMFh264Enc* enc = GST_AMFH264ENC(encoder);
    GST_DEBUG_OBJECT(enc, "stop");

    return TRUE;
}

static gboolean setup_encoder(GstAMFh264Enc* enc)
{
    guint fps_n, fps_d;
    AMF_RESULT result = AMF_FAIL;
    amf::AMFVariant p;
    
    fps_n = GST_VIDEO_INFO_FPS_N(&enc->in_state->info);
    fps_d = GST_VIDEO_INFO_FPS_D(&enc->in_state->info);

    enc->frameH = GST_VIDEO_INFO_HEIGHT(&enc->in_state->info);
    enc->frameW = GST_VIDEO_INFO_WIDTH(&enc->in_state->info);
    enc->frame_rate = AMFConstructRate(fps_n, fps_d);
    double_t frameRateFraction =
        ((double_t)fps_d / (double_t)fps_n);
    enc->timestamp_step = AMF_SECOND * frameRateFraction;
    enc->query_wait_time = std::chrono::milliseconds(1);
    
    SET_AMF_VALUE_OR_FAIL(enc->encoder_amf, AMF_VIDEO_ENCODER_FRAMESIZE,
        AMFConstructSize(enc->frameW, enc->frameH));

    SET_AMF_VALUE_OR_FAIL(enc->encoder_amf, AMF_VIDEO_ENCODER_USAGE,
        enc->usage);

    SET_AMF_VALUE_OR_FAIL(enc->encoder_amf,
        AMF_VIDEO_ENCODER_QUALITY_PRESET,
        enc->quality_preset);

    SET_AMF_VALUE_OR_FAIL(enc->encoder_amf, AMF_VIDEO_ENCODER_PROFILE,
        enc->profile);

    SET_AMF_VALUE(enc->encoder_amf, AMF_VIDEO_ENCODER_LOWLATENCY_MODE,
        enc->low_latency_mode);

    SET_AMF_VALUE(enc->encoder_amf, AMF_VIDEO_ENCODER_PRE_ANALYSIS_ENABLE,
        (bool)enc->preencode);

    SET_AMF_VALUE_OR_FAIL(enc->encoder_amf, AMF_VIDEO_ENCODER_CABAC_ENABLE,
        enc->coding_type);
    result = enc->encoder_amf->Init(amf::AMF_SURFACE_NV12, enc->frameW,
        enc->frameH);
    if (result != AMF_OK) {
        AMF_LOG_WARNING("AMF: Failed to init encoder");
        return FALSE;
    }
    SET_AMF_VALUE(enc->encoder_amf, AMF_VIDEO_ENCODER_FRAMERATE,
        enc->frame_rate);

    AMF_RESULT res =
        enc->encoder_amf->GetProperty(AMF_VIDEO_ENCODER_EXTRADATA, &p);
    if (res == AMF_OK && p.type == amf::AMF_VARIANT_INTERFACE) {
        enc->header = amf::AMFBufferPtr(p.pInterface);
    }

    if (AMF::Instance()->GetRuntimeVersion() <
        AMF_MAKE_FULL_VERSION(1, 4, 0, 0)) {
        // Support for 1.3.x drivers.
        AMF_RESULT res =
            enc->encoder_amf->SetProperty(L"NominalRange", false);
        if (res != AMF_OK) {
            AMF_LOG_WARNING(
                "Failed to set encoder color range, error code %d.",
                result);
        }
    }
    else {
        SET_AMF_VALUE(enc->encoder_amf,
            AMF_VIDEO_ENCODER_FULL_RANGE_COLOR, false);
    }

    ///dinamic properties
    SET_AMF_VALUE_OR_FAIL(enc->encoder_amf,
        AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD, enc->rate_control);

    SET_AMF_VALUE(enc->encoder_amf, AMF_VIDEO_ENCODER_ENABLE_VBAQ,
        true);

    // Rate Control Properties
    int64_t bitrate = enc->bitrate * 1000;
    int64_t bitratePeak = enc->bitrate_peak * 1000;
    if (enc->rate_control != AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_CONSTANT_QP) {
        SET_AMF_VALUE_OR_FAIL(enc->encoder_amf,
            AMF_VIDEO_ENCODER_TARGET_BITRATE,
            bitrate);

        SET_AMF_VALUE_OR_FAIL(
            enc->encoder_amf, AMF_VIDEO_ENCODER_PEAK_BITRATE,
            bitratePeak);
    }
    SET_AMF_VALUE_OR_FAIL(enc->encoder_amf, AMF_VIDEO_ENCODER_ENFORCE_HRD,
        enc->enforce_hdr);

    SET_AMF_VALUE(enc->encoder_amf,
        AMF_VIDEO_ENCODER_HIGH_MOTION_QUALITY_BOOST_ENABLE,
        enc->motion_boost);

    // VBV Buffer
    SET_AMF_VALUE_OR_FAIL(
        enc->encoder_amf, AMF_VIDEO_ENCODER_VBV_BUFFER_SIZE,
        static_cast<amf_int64>(
            bitrate * enc->buffer_size));

    // Picture Control
    int keyinterv = enc->keyframe_interval;
    int idrperiod = keyinterv * enc->frame_rate.num;
    SET_AMF_VALUE_OR_FAIL(enc->encoder_amf, AMF_VIDEO_ENCODER_IDR_PERIOD,
        (int64_t)AMF_CLAMP(idrperiod, 0, 1000000));
    SET_AMF_VALUE(enc->encoder_amf, AMF_VIDEO_ENCODER_DE_BLOCKING_FILTER,
        enc->de_blocking_filter);
    AMF::Instance()->GetTrace()->SetGlobalLevel(
        AMF_TRACE_ERROR);

    //This property reduces polling latency.
    SET_AMF_VALUE(enc->encoder_amf, L"TIMEOUT", 50);
    enc->initialised = true;
    return TRUE;
}


static gboolean
gst_amfh264enc_set_format(GstVideoEncoder* encoder,
    GstVideoCodecState* state)
{
    GstAMFh264Enc* enc = GST_AMFH264ENC(encoder);
    GST_DEBUG_OBJECT(enc, "set_format");
    enc->in_state = state;
    GstVideoInfo info = state->info;
    GST_INFO_OBJECT (enc, "input caps: %" GST_PTR_FORMAT, state->caps);

    enc->mem_type = GST_AMF_MEM_TYPE_SYSTEM;
    int n = gst_caps_get_size(state->caps);
    for (int i = 0; i < n; i++) {
        GstCapsFeatures* orig_features = gst_caps_get_features(state->caps, i);
        if (gst_caps_features_contains(orig_features, GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY)) {
            enc->mem_type = GST_AMF_MEM_TYPE_D3D11;
            break;
        }
    }
    GstStructure *s;
    GstCaps *out_caps;
    GstVideoCodecState *output_state;

    out_caps = gst_caps_new_empty_simple ("video/x-h264");
    s = gst_caps_get_structure (out_caps, 0);
    gst_structure_set (s, "stream-format", G_TYPE_STRING, "byte-stream",
      "alignment", G_TYPE_STRING, "au", NULL);
    output_state = gst_video_encoder_set_output_state (GST_VIDEO_ENCODER (enc),
      out_caps, state);

    GST_INFO_OBJECT (enc, "output caps: %" GST_PTR_FORMAT, output_state->caps);
    gst_video_codec_state_unref (output_state);

    switch(enc->mem_type)
    {
        case GST_AMF_MEM_TYPE_D3D11:
            break;//waiting for first frame to get device
        case GST_AMF_MEM_TYPE_SYSTEM:
        {
            AMF_RESULT result = AMF_FAIL;
            result = init_d3d11(enc->device_num, enc);
            if (result != AMF_OK) {
                GST_ERROR_OBJECT(enc, "Failed to create d3d11 device.");
                return FALSE;
            }
            ID3D11Device* handle = gst_d3d11_device_get_device_handle(enc->device);
            result = enc->context->InitDX11(handle, amf::AMF_DX11_1);
            if (result != AMF_OK) {
                GST_ERROR_OBJECT(enc, "Failed to init from d3d11.");
                return FALSE;
            }

            // Create Encoder
            result = AMF::Instance()->GetFactory()->CreateComponent(
                    enc->context, AMFVideoEncoderVCE_AVC,
                    &enc->encoder_amf);
            if (result != AMF_OK) {
                GST_ERROR_OBJECT(enc, "Failed to create h264 encoder.");
                return FALSE;
            }   
            return setup_encoder(enc);   
        }
        default:
        {
            GST_ERROR_OBJECT(enc, "Unsupported memory type.");
            return FALSE;
        }        
    }
 
    return TRUE;
}

static gboolean
gst_amfh264enc_propose_allocation(GstVideoEncoder* encoder, GstQuery* query)
{
    gst_query_add_allocation_meta(query, GST_VIDEO_META_API_TYPE, NULL);

    return TRUE;
}

static GstFlowReturn
gst_amfh264enc_finish(GstVideoEncoder* encoder)
{
    GstAMFh264Enc* enc = GST_AMFH264ENC(encoder);
    GST_DEBUG_OBJECT(enc, "finish");
    switch(enc->mem_type)
    {
        case GST_AMF_MEM_TYPE_D3D11:
        {
            if (enc->device)
                gst_object_unref (enc->device);
            break;
        }
        case GST_AMF_MEM_TYPE_SYSTEM:
        {
            gst_clear_object (&enc->device);
        }
        default:
        {
            break;
        }        
    }
    
    return GST_FLOW_OK;
}

static GstFlowReturn
gst_amfh264enc_handle_frame(GstVideoEncoder* encoder,
    GstVideoCodecFrame* frame)
{
    if (!frame)
        return GST_FLOW_OK;
    GstAMFh264Enc* enc = GST_AMFH264ENC(encoder);
    amf::AMFDataPtr pData = NULL;
    amf::AMFDataPtr pOutData = NULL;
    amf::AMFSurfacePtr surface;
    AMF_RESULT res = AMF_FAIL;
    GstVideoFrame vframe;
    GstVideoInfo *info = &enc->in_state->info;
    GstMapFlags in_map_flags = GST_MAP_READ;
    if (!gst_video_frame_map (&vframe, info, frame->input_buffer, in_map_flags)) {
        gst_video_codec_frame_unref(frame);
        return GST_FLOW_ERROR;
    }
    for (int i = 0; i < gst_buffer_n_memory(frame->input_buffer); i++) {
        switch(enc->mem_type)
        {
            case GST_AMF_MEM_TYPE_D3D11:
            {
                GstD3D11Memory* mem = (GstD3D11Memory*)gst_buffer_peek_memory(frame->input_buffer, i);
                    
                if (!enc->initialised)
                {
                    ID3D11Device* handle = gst_d3d11_device_get_device_handle(mem->device);
                    
                    res = enc->context->InitDX11(handle, amf::AMF_DX11_1);
                    if (res != AMF_OK) {
                        GST_ERROR_OBJECT(enc, "Failed to init from dx11.");
                        gst_video_frame_unmap (&vframe);
                        gst_video_codec_frame_unref(frame);
                        return GST_FLOW_ERROR;
                    }
                    enc->device = (GstD3D11Device *)gst_object_ref(mem->device);
                    res = AMF::Instance()->GetFactory()->CreateComponent(
                            enc->context, AMFVideoEncoderVCE_AVC,
                            &enc->encoder_amf);
                    if (res != AMF_OK) {
                        GST_ERROR_OBJECT(enc, "Failed to create h264 encoder.");
                        gst_video_frame_unmap (&vframe);
                        gst_video_codec_frame_unref(frame);
                        return GST_FLOW_ERROR;
                    }
                    setup_encoder(enc);
                }
                
                CComPtr<ID3D11Texture2D> input_tex = gst_d3d11_memory_get_texture_handle(mem);
                int in_subresource_index = gst_d3d11_memory_get_subresource_index (mem);
                static const GUID AMFTextureArrayIndexGUID = { 0x28115527, 0xe7c3, 0x4b66, { 0x99, 0xd3, 0x4f, 0x2a, 0xe6, 0xb4, 0x7f, 0xaf } };
                input_tex->SetPrivateData(AMFTextureArrayIndexGUID, sizeof(in_subresource_index), &in_subresource_index);
                res = enc->context->CreateSurfaceFromDX11Native(input_tex, &surface, NULL);
                if (res != AMF_OK) {
                    AMF_LOG_ERROR(
                        "CreateSurfaceFromDX11Native() failed  with error:  %ls\n",
                        AMF::Instance()->GetTrace()->GetResultText(res));
                    gst_video_frame_unmap (&vframe);
                    gst_video_codec_frame_unref(frame);
                    return GST_FLOW_ERROR;
                }
                break;
            }
            case GST_AMF_MEM_TYPE_SYSTEM:
            {
                res = enc->context->AllocSurface(amf::AMF_MEMORY_HOST, amf::AMF_SURFACE_NV12, enc->frameW, enc->frameH, &surface);
                if (res != AMF_OK) {
                    GST_ERROR_OBJECT(enc, "Failed to create surface. Error:  %ls\n",
                        AMF::Instance()->GetTrace()->GetResultText(res));
                    gst_video_frame_unmap (&vframe);
                    gst_video_codec_frame_unref(frame);
                    return GST_FLOW_ERROR;
                }

                guint8 *src, *dest;
                int planes = planes = surface->GetPlanesCount();
                amf::AMFPlanePtr plane;
                int dststride, srcstride, w, h;
                for (int q = 0; q < planes; q++) 
                {
                    plane = surface->GetPlaneAt(q);
                    w = GST_VIDEO_FRAME_COMP_WIDTH (&vframe, q);
                    h = GST_VIDEO_FRAME_COMP_HEIGHT (&vframe, q);
                    srcstride = GST_VIDEO_FRAME_PLANE_STRIDE (&vframe, q);
                    dststride = plane->GetHPitch();
                    src = (guint8  *)GST_VIDEO_FRAME_PLANE_DATA (&vframe, q);
                    dest = (guint8  *)plane->GetNative();

                    for (int j = 0; j < h; j++) 
                    {
                        memcpy (dest, src, w * 2);
                        dest += dststride;
                        src += srcstride;
                    }
                }
                
                break;
            }
        }

        int64_t tsLast = (int64_t)round((frame->pts - 1) * enc->timestamp_step);
        int64_t tsNow = (int64_t)round(frame->pts * enc->timestamp_step);

        surface->SetPts(tsNow);
        surface->SetProperty(AMF_PRESENT_TIMESTAMP, frame->pts);
        surface->SetDuration(tsNow - tsLast);  

        res = enc->encoder_amf->SubmitInput(surface);
        if (res != AMF_OK) {
            AMF_LOG_ERROR("Fialed to sub  with error:  %ls\n",
                        AMF::Instance()->GetTrace()->GetResultText(res));
        }
        while (true) {
            res = enc->encoder_amf->QueryOutput(&pOutData);
            if (res == AMF_OK)
                break;
            switch (res) {
                case AMF_NEED_MORE_INPUT: {
                    gst_video_frame_unmap (&vframe);
                    gst_video_codec_frame_unref(frame);
                    return GST_FLOW_OK;
                }
                case AMF_REPEAT: {
                    continue;
                }
                default: {
                    AMF_LOG_WARNING(
                        "Fialed to QueryOutput  with code: %ls\n",
                        AMF::Instance()->GetTrace()->GetResultText(res));
                    break; 
                }
            }

            std::this_thread::sleep_for(enc->query_wait_time);
        }
    }
    amf::AMFBufferPtr packetData = amf::AMFBufferPtr(pOutData);
    if (!packetData)
    {
        gst_video_frame_unmap (&vframe);
        gst_video_codec_frame_unref(frame);
        return GST_FLOW_ERROR;
    }
    frame->output_buffer =
        gst_video_encoder_allocate_output_buffer(encoder, packetData->GetSize());
    gst_buffer_fill(frame->output_buffer, 0, packetData->GetNative(),
        packetData->GetSize());
    uint64_t pktType;
    packetData->GetProperty(AMF_VIDEO_ENCODER_OUTPUT_DATA_TYPE, &pktType);
    if (pktType == AMF_VIDEO_ENCODER_OUTPUT_DATA_TYPE_IDR) {
        GST_VIDEO_CODEC_FRAME_SET_SYNC_POINT(frame);
    }
    else {
        GST_VIDEO_CODEC_FRAME_UNSET_SYNC_POINT(frame);
    }
    gst_video_frame_unmap (&vframe);
    
    return gst_video_encoder_finish_frame(encoder, frame);
}