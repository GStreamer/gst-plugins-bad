#include "gstamfencoder.h"
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/base/base.h>
#include <gst/video/video.h>
#include <gst/video/gstvideoencoder.h>
#include <string.h>
#include "gstamf.hpp"
#include "AMF/include/components/VideoEncoderHEVC.h"
#include "AMF/include/components/VideoEncoderVCE.h"
#include <thread>
#include <chrono>
#if defined(_WIN32)
#include <gst/d3d11/gstd3d11memory.h>
#include <gst/d3d11/gstd3d11bufferpool.h>
#include <gst/d3d11/gstd3d11utils.h>
#endif
#include <cmath>
#define ATTACHED_FRAME_REF L"frame_ref"

G_DEFINE_ABSTRACT_TYPE (GstAMFBaseEnc, gst_amf_base_enc,
    GST_TYPE_VIDEO_ENCODER);
GST_DEBUG_CATEGORY_EXTERN (gst_amfenc_debug);
#define GST_CAT_DEFAULT gst_amfenc_debug

static void gst_amf_base_enc_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec);
static void gst_amf_base_enc_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec);
static void gst_amf_base_enc_finalize (GObject * object);
static gboolean gst_amf_base_enc_start (GstVideoEncoder * encoder);
static gboolean gst_amf_base_enc_stop (GstVideoEncoder * encoder);
static GstFlowReturn gst_amf_base_enc_handle_frame (GstVideoEncoder * encoder,
    GstVideoCodecFrame * frame);
static GstFlowReturn gst_amf_base_enc_finish (GstVideoEncoder * encoder);
static gboolean gst_amf_base_enc_propose_allocation (GstVideoEncoder * encoder,
    GstQuery * query);
static gboolean amf_base_enc_element_init (GstPlugin * plugin);
static gboolean gst_amf_base_enc_stop_processing_thread (GstAMFBaseEnc * enc);
static gboolean gst_amf_base_enc_drain_encoder (GstAMFBaseEnc * enc);
static gboolean gst_amf_start_processing_thread (GstAMFBaseEnc * enc);

static AMF_RESULT
amf_set_property_buffer (amf::AMFSurface * object, const wchar_t * name,
    amf::AMFBuffer * val)
{
  AMF_RESULT res;
  amf::AMFVariant var (val);
  res = object->SetProperty (name, var);
  return res;
}

static AMF_RESULT
amf_get_property_buffer (amf::AMFData * object, const wchar_t * name,
    amf::AMFBuffer ** val)
{
  AMF_RESULT res;
  amf::AMFVariant var;
  res = object->GetProperty (name, &var);
  if (res != AMF_OK) {
    return res;
  }
  amf::AMFGuid guid_AMFBuffer = amf::AMFBuffer::IID ();
  amf::AMFInterface * amf_interface = var.ToInterface ();
  res = amf_interface->QueryInterface (guid_AMFBuffer, (void **) val);
  return res;
}

static
    amf::AMFBuffer *
amf_create_buffer_with_frame_ref (GstVideoCodecFrame * frame,
    amf::AMFContext * context)
{
  amf::AMFBuffer * frame_ref_storage_buffer = NULL;
  AMF_RESULT res;
  res =
      context->AllocBuffer (amf::AMF_MEMORY_HOST, sizeof (&frame),
      &frame_ref_storage_buffer);
  if (res == AMF_OK) {
    GstVideoCodecFrame **pointer =
        (GstVideoCodecFrame **) frame_ref_storage_buffer->GetNative ();
    *pointer = frame;
  }
  return frame_ref_storage_buffer;
}

static AMF_RESULT
amf_get_frame_ref (amf::AMFData * object, const wchar_t * name,
    GstVideoCodecFrame ** frame)
{
  amf::AMFBuffer * val;
  AMF_RESULT res;
  res = amf_get_property_buffer (object, name, &val);
  if (res == AMF_OK) {
    GstVideoCodecFrame **frameP = (GstVideoCodecFrame **) val->GetNative ();
    *frame = *frameP;
  }
  val->Release ();
  return res;
}

static AMF_RESULT
amf_attach_ref_texture (amf::AMFSurface * object, GstVideoCodecFrame * frame,
    const wchar_t * name, amf::AMFContext * context)
{
  amf::AMFBuffer * val = amf_create_buffer_with_frame_ref (frame, context);
  AMF_RESULT res;
  res = amf_set_property_buffer (object, name, val);
  return res;
}


void
gst_amf_enc_set_latency (GstAMFBaseEnc * encoder)
{
  GstVideoInfo *info = &encoder->in_state->info;
  guint max_delayed_frames;
  GstClockTime latency;

  if (!encoder->initialised) {
    /* FIXME get a real value from the encoder */
    max_delayed_frames = 8;
  } else {
    GList *frames = gst_video_encoder_get_frames (GST_VIDEO_ENCODER (encoder));
    max_delayed_frames = g_list_length (frames);
    g_list_free_full (frames, (GDestroyNotify) gst_video_codec_frame_unref);
  }

  if (info->fps_n) {
    latency = gst_util_uint64_scale_ceil (GST_SECOND * info->fps_d,
        max_delayed_frames, info->fps_n);
  } else {
    /* FIXME: Assume 25fps. This is better than reporting no latency at
     * all and then later failing in live pipelines
     */
    latency = gst_util_uint64_scale_ceil (GST_SECOND * 1,
        max_delayed_frames, 25);
  }

  GST_INFO_OBJECT (encoder,
      "Updating latency to %" GST_TIME_FORMAT " (%d frames)",
      GST_TIME_ARGS (latency), max_delayed_frames);

  gst_video_encoder_set_latency (GST_VIDEO_ENCODER (encoder), latency, latency);
}

static void
gst_amf_base_enc_class_init (GstAMFBaseEncClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstVideoEncoderClass *video_encoder_class = GST_VIDEO_ENCODER_CLASS (klass);

  GstCaps *sink_caps = NULL;
  GstCaps *src_caps = NULL;

  sink_caps = gst_caps_from_string (
#if defined(_WIN32)
      "video/x-raw("
      GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY "), format = (string) NV12; "
#endif
      "video/x-raw, format = (string) NV12");

  src_caps = gst_caps_from_string ("video/x-_base_"
      ", stream-format= (string) { avc, avc3, byte-stream }, "
      "alignment= (string) au, "
      "profile = (string) { high, progressive-high, constrained-high, main, constrained-baseline, baseline }");

  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS, sink_caps));
  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS, src_caps));

  gobject_class->set_property = gst_amf_base_enc_set_property;
  gobject_class->get_property = gst_amf_base_enc_get_property;
  gobject_class->finalize = gst_amf_base_enc_finalize;
  video_encoder_class->start = GST_DEBUG_FUNCPTR (gst_amf_base_enc_start);
  video_encoder_class->stop = GST_DEBUG_FUNCPTR (gst_amf_base_enc_stop);
  video_encoder_class->handle_frame =
      GST_DEBUG_FUNCPTR (gst_amf_base_enc_handle_frame);
  video_encoder_class->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_amf_base_enc_propose_allocation);
  video_encoder_class->finish = GST_DEBUG_FUNCPTR (gst_amf_base_enc_finish);

  g_object_class_install_property (gobject_class, PROP_DEVICE_NUM,
      g_param_spec_int ("device-num",
          "Device Number",
          "Set the GPU device to use for operations (-1 = auto)",
          -1, G_MAXINT, -1,
          (GParamFlags) (G_PARAM_READWRITE | GST_PARAM_MUTABLE_READY |
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
}

#if defined(_WIN32)
AMF_RESULT
init_d3d11 (uint32_t adapterIndex, GstAMFBaseEnc * enc)
{
  enc->device =
      gst_d3d11_device_new (adapterIndex, D3D11_CREATE_DEVICE_BGRA_SUPPORT);
  if (!enc->device)
    return AMF_FAIL;
  guint device_id = 0;
  guint vendor_id = 0;
  gchar *desc = NULL;
  g_object_get (enc->device, "device-id", &device_id, "vendor-id", &vendor_id,
      "description", &desc, NULL);
  if (vendor_id != 0x1002) {
    AMF_LOG_ERROR ("D3D11CreateDevice failed. Invalid vendor.");
    gst_object_unref (enc->device);
    return AMF_FAIL;
  }

  return AMF_OK;
}
#endif

static void
gst_amf_base_enc_init (GstAMFBaseEnc * enc)
{
  AMF_RESULT result = AMF_FAIL;
  if (!gst_amf_ensure_element_context (GST_ELEMENT_CAST (enc), &enc->amf_ctx)) {
    GST_ERROR_OBJECT (enc, "failed to create AMF context");
    return;
  }
  amf::AMFFactory * factory = GetFactory (enc->amf_ctx);
  result = factory->CreateContext (&enc->context);
  if (result != AMF_OK) {
    AMF_LOG_WARNING ("CreateContext Failed");
    return;
  }
}

void
gst_amf_base_enc_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstAMFBaseEnc *amf_base_enc = GST_AMF_BASE_ENC (object);

  GST_DEBUG_OBJECT (amf_base_enc, "set_property");
  switch (property_id) {
    case PROP_DEVICE_NUM:
      amf_base_enc->device_num = g_value_get_int (value);
      break;
    case PROP_BITRATE:
      amf_base_enc->bitrate = g_value_get_uint (value);
      break;
    case PROP_BITRATE_PEAK:
      amf_base_enc->bitrate_peak = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_amf_base_enc_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstAMFBaseEnc *enc = GST_AMF_BASE_ENC (object);

  GST_DEBUG_OBJECT (enc, "get_property");
  switch (property_id) {
    case PROP_DEVICE_NUM:
      g_value_set_int (value, enc->device_num);
      break;
    case PROP_BITRATE:
      g_value_set_uint (value, enc->bitrate);
      break;
    case PROP_BITRATE_PEAK:
      g_value_set_uint (value, enc->bitrate_peak);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_amf_base_enc_finalize (GObject * object)
{
  GstAMFBaseEnc *enc = GST_AMF_BASE_ENC (object);

  GST_DEBUG_OBJECT (enc, "finalize");
}

static gboolean
gst_amf_base_enc_start (GstVideoEncoder * encoder)
{
  GstAMFBaseEnc *enc = GST_AMF_BASE_ENC (encoder);
  GST_DEBUG_OBJECT (enc, "start");
  enc->pending_queue = g_async_queue_new ();
  return TRUE;
}

static gboolean
gst_amf_base_enc_stop (GstVideoEncoder * encoder)
{
  GstAMFBaseEnc *enc = GST_AMF_BASE_ENC (encoder);
  GST_DEBUG_OBJECT (enc, "stop");
  gst_amf_base_enc_stop_processing_thread (enc);
  if (enc->pending_queue) {
    g_async_queue_unref (enc->pending_queue);
    enc->pending_queue = NULL;
  }
  return TRUE;
}

static gboolean
gst_amf_base_enc_propose_allocation (GstVideoEncoder * encoder,
    GstQuery * query)
{
  gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL);

  return TRUE;
}

static GstFlowReturn
gst_amf_base_enc_finish (GstVideoEncoder * encoder)
{
  GstAMFBaseEnc *enc = GST_AMF_BASE_ENC (encoder);
  GST_DEBUG_OBJECT (enc, "finish");
  gst_amf_base_enc_stop_processing_thread (enc);
#if defined(_WIN32)
  switch (enc->mem_type) {
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
#endif
  return GST_FLOW_OK;
}

static int counter = 0;
static gpointer
gst_amf_base_enc_processing_thread (gpointer user_data)
{
  GstVideoEncoder *encoder = (GstVideoEncoder *) user_data;
  GstAMFBaseEnc *enc = (GstAMFBaseEnc *) user_data;
  GstVideoCodecFrame *frame = NULL;
  amf::AMFSurface * pending_frame = NULL;
  amf::AMFDataPtr pOutData = NULL;
  AMF_RESULT res = AMF_FAIL;

  while (true) {
    res = enc->encoder_amf->QueryOutput (&pOutData);
    if (res == AMF_EOF) {
      GST_INFO_OBJECT (enc, "exiting thread");
      break;
    }
    switch (res) {
      case AMF_NEED_MORE_INPUT:{
        break;
      }
      case AMF_REPEAT:{
        break;
      }
      case AMF_OK:{
        break;
      }
      default:{
        AMF_LOG_WARNING ("Fialed to QueryOutput  with code: %ls\n",
            GetTrace (enc->amf_ctx)->GetResultText (res));
        break;
      }
    }
    if (res != AMF_OK) {
      if (g_async_queue_length (enc->pending_queue) > 0) {
        pending_frame =
            (amf::AMFSurface *) g_async_queue_pop (enc->pending_queue);
#if defined(_WIN32)
        if (enc->mem_type == GST_AMF_MEM_TYPE_D3D11) {
          amf_get_frame_ref (pending_frame, ATTACHED_FRAME_REF, &frame);
          GstD3D11Memory *mem =
              (GstD3D11Memory *) gst_buffer_peek_memory (frame->input_buffer,
              0);
          CComPtr < ID3D11Texture2D > input_tex =
              gst_d3d11_memory_get_texture_handle (mem);
          int in_subresource_index =
              gst_d3d11_memory_get_subresource_index (mem);
          static const GUID AMFTextureArrayIndexGUID =
              { 0x28115527, 0xe7c3, 0x4b66, {0x99, 0xd3, 0x4f, 0x2a, 0xe6, 0xb4,
              0x7f, 0xaf}
          };
          input_tex->SetPrivateData (AMFTextureArrayIndexGUID,
              sizeof (in_subresource_index), &in_subresource_index);
        }
#endif
        res = enc->encoder_amf->SubmitInput (pending_frame);
        if (res == AMF_OK) {
          pending_frame->Release ();
        } else {
          g_async_queue_push_front (enc->pending_queue, pending_frame);
          std::this_thread::sleep_for (enc->query_wait_time);
        }
      }
      continue;
    }
    amf::AMFBufferPtr packetData = amf::AMFBufferPtr (pOutData);
    if (!packetData) {
      continue;
    }
    amf_get_frame_ref (pOutData, ATTACHED_FRAME_REF, &frame);
    if (!frame) {
      continue;
    }
    frame->output_buffer =
        gst_buffer_new_allocate (NULL, packetData->GetSize (), NULL);
    gst_buffer_fill (frame->output_buffer, 0, packetData->GetNative (),
        packetData->GetSize ());

    if (enc->is_sync_point (packetData)) {
      GST_VIDEO_CODEC_FRAME_SET_SYNC_POINT (frame);
    } else {
      GST_VIDEO_CODEC_FRAME_UNSET_SYNC_POINT (frame);
    }

    gst_video_encoder_finish_frame (encoder, frame);
  }
  return NULL;
}

static gboolean
gst_amf_base_enc_stop_processing_thread (GstAMFBaseEnc * enc)
{
  if (enc->processing_thread == NULL)
    return TRUE;
  AMF_RESULT res;
  GST_VIDEO_ENCODER_STREAM_UNLOCK (enc);
  while (true) {
    if (g_async_queue_length (enc->pending_queue) > 0) {
      std::this_thread::sleep_for (enc->query_wait_time);
      continue;
    }
    res = enc->encoder_amf->Drain ();
    if (res == AMF_OK)
      break;
    std::this_thread::sleep_for (enc->query_wait_time);
  }
  g_thread_join (enc->processing_thread);
  GST_VIDEO_ENCODER_STREAM_LOCK (enc);
  enc->processing_thread = NULL;
  return TRUE;
}

static gboolean
gst_amf_start_processing_thread (GstAMFBaseEnc * enc)
{
  gchar *name = g_strdup_printf ("%s-query-output", GST_OBJECT_NAME (enc));

  g_assert (enc->processing_thread == NULL);
  enc->processing_thread =
      g_thread_try_new (name, gst_amf_base_enc_processing_thread, enc, NULL);
  g_free (name);
  return TRUE;
}

static GstFlowReturn
gst_amf_base_enc_handle_frame (GstVideoEncoder * encoder,
    GstVideoCodecFrame * frame)
{
  GstAMFBaseEnc *enc = GST_AMF_BASE_ENC (encoder);
  if (!enc->initialised) {
    if (enc->init_encoder (encoder, frame) != TRUE) {
      GST_ERROR_OBJECT (enc, "Failed to encoder from frame device.");
      gst_video_codec_frame_unref (frame);
      return GST_FLOW_ERROR;
    }
  }
  amf::AMFDataPtr pData = NULL;
  amf::AMFSurface * surface;
  amf::AMFSurface * pending_frame = NULL;
  AMF_RESULT res = AMF_FAIL;
  GstVideoFrame vframe;
  GstVideoInfo *info = &enc->in_state->info;
  GstMapFlags in_map_flags = GST_MAP_READ;
  if (!gst_video_frame_map (&vframe, info, frame->input_buffer, in_map_flags)) {
    gst_video_codec_frame_unref (frame);
    return GST_FLOW_ERROR;
  }
  if (enc->processing_thread == NULL) {
    if (!gst_amf_start_processing_thread (enc)) {
      gst_video_frame_unmap (&vframe);
      return GST_FLOW_ERROR;
    }
  }

  for (int i = 0; i < gst_buffer_n_memory (frame->input_buffer); i++) {
    switch (enc->mem_type) {
#if defined(_WIN32)
      case GST_AMF_MEM_TYPE_D3D11:
      {
        GstD3D11Memory *mem =
            (GstD3D11Memory *) gst_buffer_peek_memory (frame->input_buffer, i);
        CComPtr < ID3D11Texture2D > input_tex =
            gst_d3d11_memory_get_texture_handle (mem);
        int in_subresource_index = gst_d3d11_memory_get_subresource_index (mem);
        static const GUID AMFTextureArrayIndexGUID =
            { 0x28115527, 0xe7c3, 0x4b66, {0x99, 0xd3, 0x4f, 0x2a, 0xe6, 0xb4,
            0x7f, 0xaf}
        };
        input_tex->SetPrivateData (AMFTextureArrayIndexGUID,
            sizeof (in_subresource_index), &in_subresource_index);
        res =
            enc->context->CreateSurfaceFromDX11Native (input_tex, &surface,
            NULL);
        if (res != AMF_OK) {
          AMF_LOG_ERROR
              ("CreateSurfaceFromDX11Native() failed  with error:  %ls\n",
              GetTrace (enc->amf_ctx)->GetResultText (res));
          gst_video_frame_unmap (&vframe);
          gst_video_codec_frame_unref (frame);
          return GST_FLOW_ERROR;
        }

        break;
      }
#endif
      case GST_AMF_MEM_TYPE_SYSTEM:
      {
        res =
            enc->context->AllocSurface (amf::AMF_MEMORY_HOST,
            amf::AMF_SURFACE_NV12, enc->frameW, enc->frameH, &surface);
        if (res != AMF_OK) {
          GST_ERROR_OBJECT (enc, "Failed to create surface. Error:  %ls\n",
              GetTrace (enc->amf_ctx)->GetResultText (res));
          gst_video_frame_unmap (&vframe);
          gst_video_codec_frame_unref (frame);
          return GST_FLOW_ERROR;
        }

        guint8 *src, *dest;
        int planes = planes = surface->GetPlanesCount ();
        amf::AMFPlanePtr plane;
        int dststride, srcstride, w, h;
        for (int q = 0; q < planes; q++) {
          plane = surface->GetPlaneAt (q);
          w = GST_VIDEO_FRAME_COMP_WIDTH (&vframe, q);
          h = GST_VIDEO_FRAME_COMP_HEIGHT (&vframe, q);
          srcstride = GST_VIDEO_FRAME_PLANE_STRIDE (&vframe, q);
          dststride = plane->GetHPitch ();
          src = (guint8 *) GST_VIDEO_FRAME_PLANE_DATA (&vframe, q);
          dest = (guint8 *) plane->GetNative ();

          for (int j = 0; j < h; j++) {
            memcpy (dest, src, w * 2);

            dest += dststride;
            src += srcstride;
          }
        }
        break;
      }
    }
    res =
        amf_attach_ref_texture (surface, frame, ATTACHED_FRAME_REF,
        enc->context);
    if (res != AMF_OK) {
      gst_video_frame_unmap (&vframe);
      gst_video_codec_frame_unref (frame);
      return GST_FLOW_ERROR;
    }
    int64_t tsLast = (int64_t) round ((frame->pts - 1) * enc->timestamp_step);
    int64_t tsNow = (int64_t) round (frame->pts * enc->timestamp_step);

    surface->SetPts (tsNow);
    surface->SetProperty (AMF_PRESENT_TIMESTAMP, frame->pts);
    surface->SetDuration (tsNow - tsLast);

    g_async_queue_push (enc->pending_queue, surface);
  }

  gst_video_frame_unmap (&vframe);
  return GST_FLOW_OK;
}
