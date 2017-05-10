/*  Copyright (C) 2015-2016 Setplex. All right reserved.
    This file is part of Rixjob.
    Rixjob is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
    Rixjob is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
    You should have received a copy of the GNU General Public License
    along with Rixjob.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstnvh264dec.h"

#include <string.h>             /* for memcpy */
#include <cuviddec.h>
#include <nvcuvid.h>

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideodecoder.h>
#include <gst/codecparsers/gsth264parser.h>

#define PAD_ALIGN(x, mask) ((x + mask) & ~mask)

#define GST_NVH264DEC_GET_PRIVATE(obj) \
  (G_TYPE_INSTANCE_GET_PRIVATE((obj), GST_TYPE_NVH264DEC, GstNvh264decPrivate))

GST_DEBUG_CATEGORY_STATIC (gst_nvh264dec_debug_category);
#define GST_CAT_DEFAULT gst_nvh264dec_debug_category

/* prototypes */
static void gst_nvh264dec_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec);
static void gst_nvh264dec_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec);

static gboolean gst_nvh264dec_start (GstVideoDecoder * decoder);
static gboolean gst_nvh264dec_stop (GstVideoDecoder * decoder);

static GstFlowReturn gst_nvh264dec_drain (GstVideoDecoder * decoder);
static gboolean gst_nvh264dec_set_format (GstVideoDecoder * decoder,
    GstVideoCodecState * state);
static gboolean gst_nvh264dec_reset (GstVideoDecoder * decoder, gboolean hard);
static GstFlowReturn gst_nvh264dec_finish (GstVideoDecoder * decoder);
static void gst_nvh264dec_negotiate (GstVideoDecoder * decoder, guint width,
    guint height);
static GstFlowReturn gst_nvh264dec_handle_frame (GstVideoDecoder * decoder,
    GstVideoCodecFrame * frame);

// GstNvh264dec init/free component functions
static gboolean gst_nvh264dec_init_cuda_ctx (GstVideoDecoder * decoder);
static gboolean gst_nvh264dec_free_cuda_ctx (GstVideoDecoder * decoder);
static gboolean gst_nvh264dec_init_parser (GstVideoDecoder * decoder);
static gboolean gst_nvh264dec_free_parser (GstVideoDecoder * decoder);
static gboolean gst_nvh264dec_init_decoder (GstVideoDecoder * decoder,
    CUVIDEOFORMAT * cuvidfmt);
static gboolean gst_nvh264dec_free_decoder (GstVideoDecoder * decoder);

static int CUDAAPI
gst_nvh264dec_handle_picture_display (void *decoder,
    CUVIDPARSERDISPINFO * cuviddisp);

enum
{ PROP_0, PROP_DEVICE_ID, N_PROPERTIES };

#define QUEUE_SIZE 20
#define SURFACE_COUNT QUEUE_SIZE

#define DEFAULT_DEVICE_ID (0)

typedef struct _GstNvh264decPrivate
{
  CUdevice cu_device;
  CUcontext cuda_ctx;
  guint device_id;

  CUvideodecoder decoder;
  CUvideoparser parser;

  CUVIDPARSERPARAMS cuparseinfo;
  CUVIDEOFORMATEX cuparse_ext;

  GstVideoCodecState *input_state;
  GstVideoCodecState *output_state;
  guchar *host_data;
  gsize host_data_size;

  GMutex queue_mutex;
  CUVIDPARSERDISPINFO frames[QUEUE_SIZE];
  gboolean is_frame_in_use[QUEUE_SIZE];
  int frames_in_queue;
  int read_position;
  gboolean is_end_of_decode;

  guint width;
  guint height;
} GstNvh264decPrivate;

static gboolean
check_cu (GstNvh264dec * nvh264dec, CUresult err, const char *func)
{
  const char *err_name;
  const char *err_string;

  if (err == CUDA_SUCCESS) {
    return TRUE;
  }

  cuGetErrorName (err, &err_name);
  cuGetErrorString (err, &err_string);

  if (err_name && err_string) {
    GST_WARNING_OBJECT (nvh264dec, "%s failed -> %s: %s", func, err_name,
        err_string);
  } else {
    GST_WARNING_OBJECT (nvh264dec, "%s failed", func);
  }

  return FALSE;
}

#define IS_CUDA_CALL_SUCCSESS(nvh264dec, x) check_cu(nvh264dec, x, #x)

/* pad templates */

static GstStaticPadTemplate gst_nvh264dec_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS
    ("video/x-h264, stream-format=(string)byte-stream, "
        "alignment=(string)nal"));

static GstStaticPadTemplate gst_nvh264dec_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("NV12")));

/* class initialization */

G_DEFINE_TYPE_WITH_CODE (GstNvh264dec,
    gst_nvh264dec,
    GST_TYPE_VIDEO_DECODER,
    GST_DEBUG_CATEGORY_INIT
    (gst_nvh264dec_debug_category, "nvh264dec", 0,
        "debug category for nvh264dec element"));

static void
gst_nvh264dec_class_init (GstNvh264decClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstVideoDecoderClass *video_decoder_class = GST_VIDEO_DECODER_CLASS (klass);

  g_type_class_add_private (klass, sizeof (GstNvh264decPrivate));

  /* Setting up pads and setting metadata should be moved to
     base_class_init if you intend to subclass this class. */
  gst_element_class_add_pad_template (GST_ELEMENT_CLASS (klass),
      gst_static_pad_template_get (&gst_nvh264dec_sink_template));
  gst_element_class_add_pad_template (GST_ELEMENT_CLASS (klass),
      gst_static_pad_template_get (&gst_nvh264dec_src_template));

  gst_element_class_set_static_metadata (GST_ELEMENT_CLASS (klass),
      "Nvidia CUVID H264 decoder",
      "Decoder/Video",
      "Nvidia CUVID H264 decoder", "Setplex, http://www.setplex.com");
  gobject_class->set_property = GST_DEBUG_FUNCPTR (gst_nvh264dec_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR (gst_nvh264dec_get_property);

  video_decoder_class->start = GST_DEBUG_FUNCPTR (gst_nvh264dec_start);
  video_decoder_class->stop = GST_DEBUG_FUNCPTR (gst_nvh264dec_stop);

  video_decoder_class->set_format =
      GST_DEBUG_FUNCPTR (gst_nvh264dec_set_format);
  video_decoder_class->drain = GST_DEBUG_FUNCPTR (gst_nvh264dec_drain);
  video_decoder_class->reset = GST_DEBUG_FUNCPTR (gst_nvh264dec_reset);
  video_decoder_class->finish = GST_DEBUG_FUNCPTR (gst_nvh264dec_finish);
  video_decoder_class->handle_frame =
      GST_DEBUG_FUNCPTR (gst_nvh264dec_handle_frame);

  /* define properties */
  g_object_class_install_property (gobject_class,
      PROP_DEVICE_ID,
      g_param_spec_uint ("device-id",
          "DeviceID",
          "Cuda device id", 0, G_MAXUINT, DEFAULT_DEVICE_ID, (GParamFlags)
          (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
}

static void
gst_nvh264dec_init (GstNvh264dec * nvh264dec)
{
  GstNvh264decPrivate *pnvh264dec = GST_NVH264DEC_GET_PRIVATE (nvh264dec);
  IS_CUDA_CALL_SUCCSESS (nvh264dec, cuInit (0));

  pnvh264dec->device_id = DEFAULT_DEVICE_ID;
  pnvh264dec->decoder = NULL;
  pnvh264dec->input_state = NULL;
  pnvh264dec->output_state = NULL;
  pnvh264dec->host_data = NULL;
  pnvh264dec->host_data_size = 0;
  pnvh264dec->width = 0;
  pnvh264dec->height = 0;
  g_mutex_init (&pnvh264dec->queue_mutex);
  memset (pnvh264dec->frames, 0, sizeof (pnvh264dec->frames));
  memset (pnvh264dec->is_frame_in_use, 0, sizeof (pnvh264dec->is_frame_in_use));
  pnvh264dec->is_end_of_decode = FALSE;
  pnvh264dec->frames_in_queue = 0;
  pnvh264dec->read_position = 0;
  nvh264dec->priv = pnvh264dec;

  gst_video_decoder_set_packetized (GST_VIDEO_DECODER (nvh264dec), TRUE);
}

void
gst_nvh264dec_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec)
{
  GstNvh264dec *nvh264dec = GST_NVH264DEC (object);

  GST_DEBUG_OBJECT (nvh264dec, "set_property");

  switch (property_id) {
    case PROP_DEVICE_ID:
      nvh264dec->priv->device_id = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_nvh264dec_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec)
{
  GstNvh264dec *nvh264dec = GST_NVH264DEC (object);

  GST_DEBUG_OBJECT (nvh264dec, "get_property");

  switch (property_id) {
    case PROP_DEVICE_ID:
      g_value_set_uint (value, nvh264dec->priv->device_id);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static int CUDAAPI
gst_nvh264dec_handle_video_sequence (void *decoder, CUVIDEOFORMAT * cuvidfmt)
{
  gboolean res =
      gst_nvh264dec_init_decoder (GST_VIDEO_DECODER (decoder), cuvidfmt);
  if (res) {
    return 1;
  }

  return 0;
}

static int CUDAAPI
gst_nvh264dec_handle_picture_decode (void *decoder, CUVIDPICPARAMS * picparams)
{
  GstNvh264dec *nvh264dec = GST_NVH264DEC (decoder);
  GstNvh264decPrivate *pnvh264dec = GST_NVH264DEC_GET_PRIVATE (decoder);
  gboolean is_ok = IS_CUDA_CALL_SUCCSESS (nvh264dec,
      cuvidDecodePicture (pnvh264dec->decoder,
          picparams));
  if (!is_ok) {
    return 0;
  }

  return 1;
}

static void
gst_nvh264dec_enqueue_frame (GstNvh264decPrivate * pnvh264dec,
    CUVIDPARSERDISPINFO * cuviddisp)
{
  pnvh264dec->is_frame_in_use[cuviddisp->picture_index] = TRUE;
  // Wait until we have a free entry in the display queue (should never block if we have enough
  // entries)
  do {
    gboolean is_frame_placed = FALSE;
    {
      g_mutex_lock (&pnvh264dec->queue_mutex);
      if (pnvh264dec->frames_in_queue < QUEUE_SIZE) {
        int writePosition =
            (pnvh264dec->read_position +
            pnvh264dec->frames_in_queue) % QUEUE_SIZE;
        pnvh264dec->frames[writePosition] = *cuviddisp;
        pnvh264dec->frames_in_queue++;
        is_frame_placed = TRUE;
      }
      g_mutex_unlock (&pnvh264dec->queue_mutex);
    }

    if (is_frame_placed) {
      break;
    }

  }
  while (!pnvh264dec->is_end_of_decode);
}

static gboolean
gst_nvh264dec_dequeue_frame (GstNvh264decPrivate * pnvh264dec,
    CUVIDPARSERDISPINFO * cuviddisp)
{
  g_mutex_lock (&pnvh264dec->queue_mutex);
  if (pnvh264dec->frames_in_queue > 0) {
    int entry = pnvh264dec->read_position;
    *cuviddisp = pnvh264dec->frames[entry];
    pnvh264dec->read_position = (entry + 1) % QUEUE_SIZE;
    pnvh264dec->frames_in_queue--;
    g_mutex_unlock (&pnvh264dec->queue_mutex);
    return TRUE;
  }

  g_mutex_unlock (&pnvh264dec->queue_mutex);
  return FALSE;
}

int CUDAAPI
gst_nvh264dec_handle_picture_display (void *decoder,
    CUVIDPARSERDISPINFO * cuviddisp)
{
  GstNvh264dec *nvh264dec = GST_NVH264DEC (decoder);
  GstNvh264decPrivate *pnvh264dec = GST_NVH264DEC_GET_PRIVATE (decoder);
  GST_DEBUG_OBJECT (nvh264dec, "gst_nvh264dec_handle_picture_decode");

  gst_nvh264dec_enqueue_frame (pnvh264dec, cuviddisp);
  return 1;
}

gboolean
gst_nvh264dec_init_cuda_ctx (GstVideoDecoder * decoder)
{
  GstNvh264dec *nvh264dec = GST_NVH264DEC (decoder);
  GstNvh264decPrivate *pnvh264dec = GST_NVH264DEC_GET_PRIVATE (decoder);

  gboolean is_ok = IS_CUDA_CALL_SUCCSESS (nvh264dec,
      cuDeviceGet (&pnvh264dec->cu_device,
          pnvh264dec->device_id));
  if (!is_ok) {
    return FALSE;
  }

  is_ok = IS_CUDA_CALL_SUCCSESS (nvh264dec,
      cuCtxCreate (&pnvh264dec->cuda_ctx,
          CU_CTX_SCHED_BLOCKING_SYNC, pnvh264dec->cu_device));
  if (!is_ok) {
    return FALSE;
  }

  return TRUE;
}

gboolean
gst_nvh264dec_free_cuda_ctx (GstVideoDecoder * decoder)
{
  GstNvh264dec *nvh264dec = GST_NVH264DEC (decoder);
  GstNvh264decPrivate *pnvh264dec = GST_NVH264DEC_GET_PRIVATE (decoder);

  if (!pnvh264dec->cuda_ctx) {
    return TRUE;
  }

  IS_CUDA_CALL_SUCCSESS (nvh264dec, cuCtxDestroy (pnvh264dec->cuda_ctx));
  pnvh264dec->cuda_ctx = NULL;
  return TRUE;
}

gboolean
gst_nvh264dec_init_parser (GstVideoDecoder * decoder)
{
  GstNvh264dec *nvh264dec = GST_NVH264DEC (decoder);
  GstNvh264decPrivate *pnvh264dec = GST_NVH264DEC_GET_PRIVATE (decoder);
  CUvideoparser lparser;
  gboolean is_ok;

  if (pnvh264dec->parser) {
    return TRUE;
  }

  memset (&pnvh264dec->cuparseinfo, 0, sizeof (CUVIDPARSERPARAMS));
  memset (&pnvh264dec->cuparse_ext, 0, sizeof (CUVIDEOFORMATEX));
  // pnvh264dec->cuparseinfo.pExtVideoInfo = &pnvh264dec->cuparse_ext;

  pnvh264dec->cuparseinfo.CodecType = cudaVideoCodec_H264;

  /*!
   * CUVIDPICPARAMS.CurrPicIdx <= kMaxDecodeSurfaces.
   * CUVIDPARSERDISPINFO.picture_index <= kMaxDecodeSurfaces
   * gst_nvh264dec_handle_picture_decode must check whether CUVIDPICPARAMS.CurrPicIdx is in use
   * gst_nvh264dec_handle_picture_display must mark CUVIDPARSERDISPINFO.picture_index is in use
   * If a frame is unmapped, mark the index not in use
   *
   */
  pnvh264dec->cuparseinfo.ulMaxNumDecodeSurfaces = SURFACE_COUNT;
  // pnvh264dec->cuparseinfo.ulMaxDisplayDelay = 4;
  pnvh264dec->cuparseinfo.pUserData = nvh264dec;
  pnvh264dec->cuparseinfo.pfnSequenceCallback =
      gst_nvh264dec_handle_video_sequence;
  pnvh264dec->cuparseinfo.pfnDecodePicture =
      gst_nvh264dec_handle_picture_decode;
  pnvh264dec->cuparseinfo.pfnDisplayPicture =
      gst_nvh264dec_handle_picture_display;

  is_ok = IS_CUDA_CALL_SUCCSESS (nvh264dec,
      cuvidCreateVideoParser (&lparser, &pnvh264dec->cuparseinfo));
  if (!is_ok) {
    return FALSE;
  }

  pnvh264dec->parser = lparser;
  return is_ok;
}

gboolean
gst_nvh264dec_free_parser (GstVideoDecoder * decoder)
{
  GstNvh264dec *nvh264dec = GST_NVH264DEC (decoder);
  GstNvh264decPrivate *pnvh264dec = GST_NVH264DEC_GET_PRIVATE (decoder);

  if (!pnvh264dec->parser) {
    return TRUE;
  }

  IS_CUDA_CALL_SUCCSESS (nvh264dec,
      cuvidDestroyVideoParser (pnvh264dec->parser));
  pnvh264dec->parser = NULL;
  return TRUE;
}

gboolean
gst_nvh264dec_init_decoder (GstVideoDecoder * decoder, CUVIDEOFORMAT * cuvidfmt)
{
  static const int MAX_FRAME_COUNT = 2;
  GstNvh264dec *nvh264dec = GST_NVH264DEC (decoder);
  GstNvh264decPrivate *pnvh264dec = GST_NVH264DEC_GET_PRIVATE (decoder);
  CUvideodecoder ldecoder;
  CUVIDDECODECREATEINFO dec_param;
  gboolean is_ok;
  unsigned int width;
  unsigned int height;

  if (pnvh264dec->decoder) {
    return TRUE;
  }

  memset (&dec_param, 0, sizeof (CUVIDDECODECREATEINFO));
  dec_param.CodecType = cuvidfmt->codec;
  width = cuvidfmt->coded_width;
  height = cuvidfmt->coded_height;

  dec_param.ulWidth = width;
  dec_param.ulHeight = height;
  pnvh264dec->width = dec_param.ulTargetWidth = PAD_ALIGN (width, 0x3F) * 4 / 3;
  pnvh264dec->height = dec_param.ulTargetHeight =
      PAD_ALIGN (height, 0x0F) * 4 / 3;

  dec_param.ulNumDecodeSurfaces = SURFACE_COUNT;

  // Limit decode memory to 24MB (16M pixels at 4:2:0 = 24M bytes)
  while (dec_param.ulNumDecodeSurfaces * dec_param.ulWidth *
      dec_param.ulHeight > 16 * 1024 * 1024) {
    dec_param.ulNumDecodeSurfaces--;
  }

  dec_param.ChromaFormat = cuvidfmt->chroma_format;
  dec_param.OutputFormat = cudaVideoSurfaceFormat_NV12;
  dec_param.DeinterlaceMode = cudaVideoDeinterlaceMode_Weave;

  dec_param.ulNumOutputSurfaces = MAX_FRAME_COUNT;      // We won't simultaneously map more than 8 surfaces
  dec_param.ulCreationFlags = cudaVideoCreate_PreferCUVID;

  is_ok = IS_CUDA_CALL_SUCCSESS (nvh264dec,
      cuvidCreateDecoder (&ldecoder, &dec_param));
  if (!is_ok) {
    return FALSE;
  }

  gst_nvh264dec_negotiate (decoder, dec_param.ulTargetWidth,
      dec_param.ulTargetHeight);
  pnvh264dec->decoder = ldecoder;
  return TRUE;
}

gboolean
gst_nvh264dec_free_decoder (GstVideoDecoder * decoder)
{
  GstNvh264dec *nvh264dec = GST_NVH264DEC (decoder);
  GstNvh264decPrivate *pnvh264dec = GST_NVH264DEC_GET_PRIVATE (decoder);

  if (!pnvh264dec->decoder) {
    return TRUE;
  }

  IS_CUDA_CALL_SUCCSESS (nvh264dec, cuvidDestroyDecoder (pnvh264dec->decoder));
  pnvh264dec->decoder = NULL;
  return TRUE;
}

gboolean
gst_nvh264dec_start (GstVideoDecoder * decoder)
{
  gboolean res = gst_nvh264dec_init_cuda_ctx (decoder);
  if (!res) {
    return FALSE;
  }
  return gst_nvh264dec_init_parser (decoder);
}

gboolean
gst_nvh264dec_stop (GstVideoDecoder * decoder)
{
  GstNvh264decPrivate *pnvh264dec = GST_NVH264DEC_GET_PRIVATE (decoder);

  pnvh264dec->is_end_of_decode = TRUE;
  gst_nvh264dec_free_decoder (decoder);
  gst_nvh264dec_free_parser (decoder);
  gst_nvh264dec_free_cuda_ctx (decoder);
  if (pnvh264dec->input_state) {
    gst_video_codec_state_unref (pnvh264dec->input_state);
    pnvh264dec->input_state = NULL;
  }
  return TRUE;
}

GstFlowReturn
gst_nvh264dec_drain (GstVideoDecoder * decoder)
{
  // GstNvh264dec *nvh264dec = GST_NVH264DEC (decoder);
  return gst_nvh264dec_handle_frame (decoder, NULL);
}

gboolean
gst_nvh264dec_set_format (GstVideoDecoder * decoder, GstVideoCodecState * state)
{
  GstNvh264dec *nvh264dec = GST_NVH264DEC (decoder);
  GstNvh264decPrivate *pnvh264dec = GST_NVH264DEC_GET_PRIVATE (decoder);
  const gchar *codec_data;
  GstStructure *pad_struct;
  gint width = 0;
  gint height = 0;

  GST_DEBUG_OBJECT (nvh264dec, "input caps: %" GST_PTR_FORMAT, state->caps);

  pad_struct = gst_caps_get_structure (state->caps, 0);
  if (gst_structure_get_int (pad_struct, "width", &width)) {
  }
  if (gst_structure_get_int (pad_struct, "height", &height)) {
  }

  codec_data = gst_structure_get_string (pad_struct, "codec_data");
  if (codec_data) {
  }

  if (pnvh264dec->input_state) {
    gst_video_codec_state_unref (pnvh264dec->input_state);
    pnvh264dec->input_state = NULL;
  }
  pnvh264dec->input_state = gst_video_codec_state_ref (state);
  return TRUE;
}

gboolean
gst_nvh264dec_reset (GstVideoDecoder * decoder, gboolean hard)
{
  GstNvh264dec *nvh264dec = GST_NVH264DEC (decoder);
  GST_DEBUG_OBJECT (nvh264dec, "reset");
  return TRUE;
}

GstFlowReturn
gst_nvh264dec_finish (GstVideoDecoder * decoder)
{
  GstNvh264dec *nvh264dec = GST_NVH264DEC (decoder);
  GST_DEBUG_OBJECT (nvh264dec, "finish");
  return GST_FLOW_OK;
}

void
gst_nvh264dec_negotiate (GstVideoDecoder * decoder, guint width, guint height)
{
  GstNvh264decPrivate *pnvh264dec = GST_NVH264DEC_GET_PRIVATE (decoder);
  GstVideoFormat format = GST_VIDEO_FORMAT_NV12;
  if (pnvh264dec->output_state) {
    GstVideoInfo *info = &pnvh264dec->output_state->info;
    if (width == GST_VIDEO_INFO_WIDTH (info)
        && height == GST_VIDEO_INFO_HEIGHT (info)
        && format == GST_VIDEO_INFO_FORMAT (info)) {
      gst_video_codec_state_unref (pnvh264dec->output_state);
      pnvh264dec->output_state = NULL;
      return;
    }
    gst_video_codec_state_unref (pnvh264dec->output_state);
    pnvh264dec->output_state = NULL;
  }

  pnvh264dec->output_state =
      gst_video_decoder_set_output_state (decoder, format, width, height,
      pnvh264dec->input_state);
  gst_video_decoder_negotiate (decoder);
}

static GstFlowReturn
gst_nvh264dec_send_decoded_frame (GstVideoDecoder * decoder,
    CUVIDPARSERDISPINFO * cuviddisp, GstVideoCodecFrame * frame)
{
  GstNvh264dec *nvh264dec = GST_NVH264DEC (decoder);
  GstNvh264decPrivate *pnvh264dec = GST_NVH264DEC_GET_PRIVATE (decoder);
  GstMapInfo omap_info;
  GstFlowReturn ret;
  int pitch_offset;
  int size;
  CUdeviceptr mapped_frame;
  unsigned int pitch;
  gboolean is_ok;

  CUVIDPROCPARAMS proc_params;
  memset (&proc_params, 0, sizeof (CUVIDPROCPARAMS));
  proc_params.progressive_frame = cuviddisp->progressive_frame;
  proc_params.second_field = 0;
  proc_params.top_field_first = cuviddisp->top_field_first;
  proc_params.unpaired_field = cuviddisp->progressive_frame == 1;

  is_ok = IS_CUDA_CALL_SUCCSESS (nvh264dec,
      cuvidMapVideoFrame (pnvh264dec->decoder,
          cuviddisp->picture_index, &mapped_frame, &pitch, &proc_params));
  if (!is_ok) {
    return GST_FLOW_ERROR;
  }

  pitch_offset = pitch * pnvh264dec->height;
  size = pitch_offset * 3 / 2;
  if (size > pnvh264dec->host_data_size && pnvh264dec->host_data) {
    IS_CUDA_CALL_SUCCSESS (nvh264dec, cuMemFreeHost (pnvh264dec->host_data));
    pnvh264dec->host_data = 0;
    pnvh264dec->host_data_size = 0;
  }
  if (!pnvh264dec->host_data) {
    is_ok =
        IS_CUDA_CALL_SUCCSESS (nvh264dec,
        cuMemAllocHost ((void **) &pnvh264dec->host_data, size));
    if (!is_ok) {
      IS_CUDA_CALL_SUCCSESS (nvh264dec,
          cuvidUnmapVideoFrame (pnvh264dec->decoder, mapped_frame));
      return GST_FLOW_ERROR;
    }
    pnvh264dec->host_data_size = size;
  }

  is_ok =
      IS_CUDA_CALL_SUCCSESS (nvh264dec,
      cuMemcpyDtoH (pnvh264dec->host_data, mapped_frame, size));
  if (!is_ok) {
    IS_CUDA_CALL_SUCCSESS (nvh264dec,
        cuvidUnmapVideoFrame (pnvh264dec->decoder, mapped_frame));
    return GST_FLOW_ERROR;
  }

  is_ok =
      IS_CUDA_CALL_SUCCSESS (nvh264dec,
      cuvidUnmapVideoFrame (pnvh264dec->decoder, mapped_frame));
  if (!is_ok) {
    return GST_FLOW_ERROR;
  }

  ret = gst_video_decoder_allocate_output_frame (decoder, frame);
  if (G_UNLIKELY (ret != GST_FLOW_OK)) {
    return GST_FLOW_ERROR;
  }

  if (!gst_buffer_map (frame->output_buffer, &omap_info, GST_MAP_WRITE)) {
    GST_ERROR_OBJECT (nvh264dec, "Cannot map output buffer!");
    return GST_FLOW_ERROR;
  }

  if (pnvh264dec->host_data_size != omap_info.size) {
    GST_WARNING_OBJECT (nvh264dec,
        "Cuda buffer size: %lu, not equal gstreamer buffer size: %lu",
        pnvh264dec->host_data_size, omap_info.size);
  }

  memcpy (omap_info.data, pnvh264dec->host_data, pnvh264dec->host_data_size);

  gst_buffer_unmap (frame->output_buffer, &omap_info);
  return GST_FLOW_OK;
}

GstFlowReturn
gst_nvh264dec_handle_frame (GstVideoDecoder * decoder,
    GstVideoCodecFrame * frame)
{
  GstNvh264dec *nvh264dec = GST_NVH264DEC (decoder);
  GstNvh264decPrivate *pnvh264dec = GST_NVH264DEC_GET_PRIVATE (decoder);
  gboolean is_ok;
  CUVIDSOURCEDATAPACKET cuvid_pkt;
  CUVIDPARSERDISPINFO cuviddisp;
  GstMapInfo map_info;

  if (!pnvh264dec->parser) {
    if (frame) {
      GST_ERROR_OBJECT (nvh264dec, "CUVID parser not ready");
      gst_video_codec_frame_unref (frame);
      return GST_FLOW_ERROR;
    }
    return GST_FLOW_OK;
  }

  if (!frame) {
    CUVIDSOURCEDATAPACKET cuvid_pkt;
    memset (&cuvid_pkt, 0, sizeof (CUVIDSOURCEDATAPACKET));
    cuvid_pkt.flags = CUVID_PKT_ENDOFSTREAM;

    is_ok = IS_CUDA_CALL_SUCCSESS (nvh264dec,
        cuvidParseVideoData (pnvh264dec->parser, &cuvid_pkt));
    if (cuvid_pkt.flags & CUVID_PKT_ENDOFSTREAM || !is_ok) {
    }
    return GST_FLOW_OK;
  }

  if (!gst_buffer_map (frame->input_buffer, &map_info, GST_MAP_READ)) {
    GST_ERROR_OBJECT (nvh264dec, "Cannot map input buffer!");
    gst_video_codec_frame_unref (frame);
    return GST_FLOW_ERROR;
  }

  GST_LOG_OBJECT (nvh264dec, "handle frame, %d",
      map_info.size > 4 ? map_info.data[4] & 0x1f : -1);

  memset (&cuvid_pkt, 0, sizeof (CUVIDSOURCEDATAPACKET));
  cuvid_pkt.payload = map_info.data;
  cuvid_pkt.payload_size = map_info.size;
  cuvid_pkt.flags = CUVID_PKT_TIMESTAMP;
  if (GST_CLOCK_TIME_IS_VALID (frame->pts)) {
    cuvid_pkt.timestamp = frame->pts;
  }

  is_ok = IS_CUDA_CALL_SUCCSESS (nvh264dec,
      cuvidParseVideoData (pnvh264dec->parser, &cuvid_pkt));
  if (!is_ok) {
    gst_buffer_unmap (frame->input_buffer, &map_info);
    gst_video_codec_frame_unref (frame);
    return GST_FLOW_ERROR;
  }

  if (cuvid_pkt.flags & CUVID_PKT_ENDOFSTREAM || !is_ok) {
  }

  if (pnvh264dec->output_state
      && gst_nvh264dec_dequeue_frame (pnvh264dec, &cuviddisp)) {
    GstFlowReturn ret =
        gst_nvh264dec_send_decoded_frame (decoder, &cuviddisp, frame);
    if (G_UNLIKELY (ret != GST_FLOW_OK)) {
      gst_buffer_unmap (frame->input_buffer, &map_info);
      gst_video_codec_frame_unref (frame);
      return ret;
    }
    gst_buffer_unmap (frame->input_buffer, &map_info);
    return gst_video_decoder_finish_frame (GST_VIDEO_DECODER (nvh264dec),
        frame);
  }

  gst_buffer_unmap (frame->input_buffer, &map_info);
  gst_video_codec_frame_unref (frame);
  return GST_FLOW_OK;
}
