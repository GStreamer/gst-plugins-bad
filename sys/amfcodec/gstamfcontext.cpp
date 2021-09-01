#include "gstamfcontext.hpp"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <glib.h>
#if defined(_WIN32)
	#include <dxgi.h>
	#include <d3d11.h>
	#include <d3d11_1.h>
	#include <atlbase.h>
#endif
#include "AMF/include/components/VideoEncoderVCE.h"
#include "AMF/include/components/VideoEncoderHEVC.h"
#include "AMF/include/core/Factory.h"
#include "AMF/include/core/Data.h"
#include "AMF/include/core/PropertyStorageEx.h"
#include <gmodule.h>

GST_DEBUG_CATEGORY_STATIC (gst_amf_context_debug);
#define GST_CAT_DEFAULT gst_amf_context_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_CONTEXT);

struct _GstAMFContextPrivate
{
    GModule * amf_module;
	uint64_t amf_version;

	/// AMF Functions
	AMFQueryVersion_Fn amf_version_fun;
	AMFInit_Fn amf_init_fun;

	/// AMF Objects
	amf::AMFFactory *factory;
	amf::AMFTrace *trace;
	std::unique_ptr<GstAMFTraceWriter> trace_writer;
	std::map<int, AMFEncoderCaps> h264_caps;
	std::map<int, AMFEncoderCaps> hevc_caps;
};

G_DEFINE_TYPE_WITH_PRIVATE (GstAMFContext, gst_amf_context, GST_TYPE_OBJECT);

static void gst_amf_context_constructed (GObject * object);
static void gst_amf_context_finalize (GObject * object);

static void
gst_amf_context_class_init (GstAMFContextClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->constructed = gst_amf_context_constructed;
  gobject_class->finalize = gst_amf_context_finalize;

  GST_DEBUG_CATEGORY_INIT (gst_amf_context_debug,
      "amfcontext", 0, "AMF Context");
}

static void
gst_amf_context_init (GstAMFContext * context)
{
    GstAMFContextPrivate *priv = new GstAMFContextPrivate();//(GstAMFContextPrivate *)gst_amf_context_get_instance_private (context);
    AMF_RESULT result = AMF_FAIL;
	priv->amf_version_fun = nullptr;
	priv->amf_init_fun = nullptr;
	priv->amf_version = 0;
	priv->amf_module = g_module_open(AMF_DLL_NAMEA, G_MODULE_BIND_LAZY);
	if (!priv->amf_module) {
		return;
	}

	if (!g_module_symbol (priv->amf_module, AMF_INIT_FUNCTION_NAME,
          (gpointer *) & priv->amf_init_fun)) 
    {
		AMF_LOG_ERROR("Failed to set init function. error %s.", g_module_error());
		return;
	}
	result = priv->amf_init_fun(AMF_FULL_VERSION, &priv->factory);
	if (result != AMF_OK) {
		AMF_LOG_ERROR("Init failed.");
		return;
	}
	result = priv->factory->GetTrace(&priv->trace);
	if (result != AMF_OK) {
		AMF_LOG_ERROR("AMF: Failed to GetTrace.");
		return;
	}

	if (!g_module_symbol (priv->amf_module, AMF_QUERY_VERSION_FUNCTION_NAME,
          (gpointer *) & priv->amf_version_fun)) 
    {
		AMF_LOG_ERROR(
			"Incompatible AMF Runtime (could not find '%s')",
			AMF_QUERY_VERSION_FUNCTION_NAME);
		return;
	} 
	else 
	{
		result = priv->amf_version_fun(&priv->amf_version);

		if (result != AMF_OK) {
			AMF_LOG_ERROR(
				"Querying Version failed, error code %ls.", 
				priv->trace->GetResultText(result));
			return;
		}
	}
	priv->trace_writer = std::unique_ptr<GstAMFTraceWriter>(
		new GstAMFTraceWriter());
	priv->trace->RegisterWriter(OBS_AMF_TRACE_WRITER, priv->trace_writer.get(), true);

    context->priv = priv;
}

static void
gst_amf_context_constructed (GObject * object)
{
    GstAMFContext *context = GST_AMF_CONTEXT (object);
    GstAMFContextPrivate *priv = context->priv;

    priv->h264_caps.clear();
	priv->hevc_caps.clear();
#if defined(_WIN32)
	ATL::CComPtr<IDXGIFactory> pFactory;
	HRESULT hr;
	
	hr = CreateDXGIFactory1(__uuidof(IDXGIFactory), (void **)(&pFactory));
	if (FAILED(hr)) {
		AMF_LOG_WARNING("CreateDXGIFactory1 failed");
		return;
	}

	INT count = -1;
	while (true) {
		count++;
		AMF_RESULT result;
		ATL::CComPtr<IDXGIAdapter> pAdapter;
		ATL::CComPtr<ID3D11Device> pD3D11Device;
		ATL::CComPtr<ID3D11DeviceContext> pD3D11Context;
	
		if (pFactory->EnumAdapters(count, &pAdapter) ==
		    DXGI_ERROR_NOT_FOUND) {
			break;
		}

		DXGI_ADAPTER_DESC desc;
		pAdapter->GetDesc(&desc);

		if (desc.VendorId != 0x1002) {
			continue;
		}
		ATL::CComPtr<IDXGIOutput> pOutput;
		if (pAdapter->EnumOutputs(0, &pOutput) ==
		    DXGI_ERROR_NOT_FOUND) {
			continue;
		}

		hr = D3D11CreateDevice(pAdapter, D3D_DRIVER_TYPE_UNKNOWN, NULL,
				       0, NULL, 0, D3D11_SDK_VERSION,
				       &pD3D11Device, NULL, &pD3D11Context);
		if (FAILED(hr)) {
			AMF_LOG_WARNING("D3D11CreateDevice failed");
			continue;
		}

		amf::AMFContextPtr pContext;
		result = priv->factory->CreateContext(&pContext);
		if (result != AMF_OK) {
			continue;
		}
		result = pContext->InitDX11(pD3D11Device);
		if (result != AMF_OK) {
			continue;
		}
		amf::AMFComponentPtr encoder;
		result = priv->factory->CreateComponent(
			pContext, AMFVideoEncoderVCE_AVC, &encoder);
		if (result == AMF_OK) {
			const amf::AMFPropertyInfo *pParamInfo = NULL;

			result = encoder->GetPropertyInfo(
				AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD,
				&pParamInfo);
			if (result == AMF_OK) {
				AMFEncoderCaps caps;
				const amf::AMFEnumDescriptionEntry *enumDesc =
					pParamInfo->pEnumDescription;
				while (enumDesc->name) {
					AMFEncoderCaps::NameValuePair pair;
					pair.value = enumDesc->value;
					pair.name = enumDesc->name;
					caps.rate_control_methods.push_back(
						pair);
					enumDesc++;
				}
				priv->h264_caps.insert({count, caps});
			}
		}

		encoder.Release();
		result = priv->factory->CreateComponent(
			pContext, AMFVideoEncoder_HEVC, &encoder);
		if (result == AMF_OK) {
			const amf::AMFPropertyInfo *pParamInfo = NULL;

			result = encoder->GetPropertyInfo(
				AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD,
				&pParamInfo);
			if (result == AMF_OK) {
				AMFEncoderCaps caps;
				const amf::AMFEnumDescriptionEntry *enumDesc =
					pParamInfo->pEnumDescription;
				while (enumDesc->name) {
					AMFEncoderCaps::NameValuePair pair;
					pair.value = enumDesc->value;
					pair.name = enumDesc->name;
					caps.rate_control_methods.push_back(
						pair);
					enumDesc++;
				}
				priv->hevc_caps.insert({count, caps});
			}
		}
	}
#endif
}

static void
gst_amf_context_finalize (GObject * object)
{
    GstAMFContext *context = GST_AMF_CONTEXT_CAST (object);
    GstAMFContextPrivate *priv = context->priv;

    if (priv->amf_module) {
        if (priv->trace) {
            priv->trace->TraceFlush();
            priv->trace->UnregisterWriter(OBS_AMF_TRACE_WRITER);
        }
        g_module_close(priv->amf_module);
    }
    priv->amf_version = 0;
    priv->amf_module = 0;

    priv->factory = nullptr;
    priv->trace = nullptr;
    priv->amf_version_fun = nullptr;
    priv->amf_init_fun = nullptr;
}

GstAMFContext *
gst_amf_context_new ()
{
    GstAMFContext *self = (GstAMFContext *)
        g_object_new (GST_TYPE_AMF_CONTEXT, NULL);

    gst_object_ref_sink (self);
    return self;
}

static gboolean
pad_query (const GValue * item, GValue * value, gpointer user_data)
{
  GstPad *pad = (GstPad *)g_value_get_object (item);
  GstQuery *query = (GstQuery*)user_data;
  gboolean res;

  res = gst_pad_peer_query (pad, query);

  if (res) {
    g_value_set_boolean (value, TRUE);
    return FALSE;
  }

  GST_CAT_INFO_OBJECT (GST_CAT_CONTEXT, pad, "pad peer query failed");
  return TRUE;
}

static gboolean
run_query (GstElement * element, GstQuery * query, GstPadDirection direction)
{
  GstIterator *it;
  GstIteratorFoldFunction func = pad_query;
  GValue res = { 0 };

  g_value_init (&res, G_TYPE_BOOLEAN);
  g_value_set_boolean (&res, FALSE);

  /* Ask neighbor */
  if (direction == GST_PAD_SRC)
    it = gst_element_iterate_src_pads (element);
  else
    it = gst_element_iterate_sink_pads (element);

  while (gst_iterator_fold (it, func, &res, query) == GST_ITERATOR_RESYNC)
    gst_iterator_resync (it);

  gst_iterator_free (it);

  return g_value_get_boolean (&res);
}

static void
find_amf_context (GstElement * element, GstAMFContext ** amf_ctx)
{
  GstQuery *query;
  GstContext *ctxt;

  query = gst_query_new_context (GST_AMF_CONTEXT_TYPE);
  if (run_query (element, query, GST_PAD_SRC)) {
    gst_query_parse_context (query, &ctxt);
    GST_CAT_INFO_OBJECT (GST_CAT_CONTEXT, element,
        "found context (%p) in downstream query", ctxt);
    gst_element_set_context (element, ctxt);
  }

  if (*amf_ctx == NULL && run_query (element, query, GST_PAD_SINK)) {
    gst_query_parse_context (query, &ctxt);
    GST_CAT_INFO_OBJECT (GST_CAT_CONTEXT, element,
        "found context (%p) in upstream query", ctxt);
    gst_element_set_context (element, ctxt);
  }

  if (*amf_ctx == NULL) {
    GstMessage *msg;

    GST_CAT_INFO_OBJECT (GST_CAT_CONTEXT, element,
        "posting need context message");
    msg = gst_message_new_need_context (GST_OBJECT_CAST (element),
        GST_AMF_CONTEXT_TYPE);
    gst_element_post_message (element, msg);
    }
}

gboolean
gst_amf_ensure_element_context (GstElement * element, GstAMFContext ** amf_ctx)
{
  g_return_val_if_fail (element != NULL, FALSE);
  g_return_val_if_fail (amf_ctx != NULL, FALSE);

  if (*amf_ctx)
    return TRUE;

  find_amf_context (element, amf_ctx);
  if (*amf_ctx)
    return TRUE;

  *amf_ctx = gst_amf_context_new ();

  if (*amf_ctx == NULL) {
    GST_CAT_ERROR_OBJECT (GST_CAT_CONTEXT, element,
        "Failed to create AMF context");
    return FALSE;
  } else {
    GstContext *context;
    GstMessage *msg;

    context = gst_context_new (GST_AMF_CONTEXT_TYPE, TRUE);
    gst_element_set_context (element, context);

    GST_CAT_INFO_OBJECT (GST_CAT_CONTEXT, element,
        "posting have context (%p) message with AMF context (%p)",
        context, *amf_ctx);
    msg = gst_message_new_have_context (GST_OBJECT_CAST (element), context);
    gst_element_post_message (GST_ELEMENT_CAST (element), msg);
  }

  return TRUE;
}



amf::AMFFactory *GetFactory(GstAMFContext * amf_ctx)
{
    return amf_ctx->priv->factory;
}

amf::AMFTrace *GetTrace(GstAMFContext * amf_ctx)
{
    return amf_ctx->priv->trace;
}

uint64_t GetRuntimeVersion(GstAMFContext * amf_ctx)
{
    return amf_ctx->priv->amf_version;
}

AMFEncoderCaps GetH264Caps(GstAMFContext * amf_ctx, int deviceNum)
{
    std::map<int, AMFEncoderCaps>::const_iterator it;
	it = amf_ctx->priv->h264_caps.find(deviceNum);

	if (it != amf_ctx->priv->h264_caps.end())
		return (it->second);
	return AMFEncoderCaps();
}

AMFEncoderCaps GetHEVCCaps(GstAMFContext * amf_ctx, int deviceNum)
{
    std::map<int, AMFEncoderCaps>::const_iterator it;
	it = amf_ctx->priv->hevc_caps.find(deviceNum);

	if (it != amf_ctx->priv->hevc_caps.end())
		return (it->second);
	return AMFEncoderCaps();
}

int defaultDeviceH264(GstAMFContext * amf_ctx)
{
    if (amf_ctx->priv->h264_caps.empty())
		return -1;
	return amf_ctx->priv->h264_caps.begin()->first;
}

int defaultDeviceHEVC(GstAMFContext * amf_ctx)
{
    if (amf_ctx->priv->hevc_caps.empty())
		return -1;
	return amf_ctx->priv->hevc_caps.begin()->first;
}

bool H264Available(GstAMFContext * amf_ctx)
{
    return amf_ctx->priv->h264_caps.size() > 0;
}

bool HEVCAvailable(GstAMFContext * amf_ctx) 
{
    return amf_ctx->priv->hevc_caps.size() > 0;
}