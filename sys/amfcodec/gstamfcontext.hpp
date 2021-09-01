#ifndef __GST_AMF_CONTEXT_H__
#define __GST_AMF_CONTEXT_H__

#include <memory>
#include "AMF/include/core/Factory.h"
#include "AMF/include/core/Trace.h"
#include "AMF/include/components/VideoEncoderVCE.h"
#include "amftracewriter.hpp"
#include <map>
#include <list>
#include <memory>
#include <gst/gst.h>
#include "gstamf.hpp"
extern "C" {
#if defined(WIN32) || defined(WIN64)
#include <windows.h>
#endif
}

G_BEGIN_DECLS

#define GST_TYPE_AMF_CONTEXT             (gst_amf_context_get_type())
#define GST_AMF_CONTEXT(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_AMF_CONTEXT,GstAMFContext))
#define GST_AMF_CONTEXT_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),  GST_TYPE_AMF_CONTEXT,GstAMFContextClass))
#define GST_AMF_CONTEXT_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS((obj),  GST_TYPE_AMF_CONTEXT,GstAMFContextClass))
#define GST_IS_AMF_CONTEXT(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj),GST_TYPE_AMF_CONTEXT))
#define GST_IS_AMF_CONTEXT_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_AMF_CONTEXT))
#define GST_AMF_CONTEXT_CAST(obj)        ((GstAMFContext*)(obj))

#define GST_AMF_CONTEXT_TYPE "gst.amf.context"

typedef struct _GstAMFContext GstAMFContext;
typedef struct _GstAMFContextClass GstAMFContextClass;
typedef struct _GstAMFContextPrivate GstAMFContextPrivate;

struct AMFEncoderCaps {
	struct NameValuePair {
		int32_t value;
		std::wstring name;
	};
	std::list<NameValuePair> rate_control_methods;
};

/*
 * GstAMFContext:
 */
struct _GstAMFContext
{
  GstObject object;

  /*< private >*/
  GstAMFContextPrivate *priv;
};

/*
 * GstAMFContextClass:
 */
struct _GstAMFContextClass
{
  GstObjectClass parent_class;
};

GType            gst_amf_context_get_type    (void);

G_GNUC_INTERNAL
GstAMFContext * gst_amf_context_new         ();

G_GNUC_INTERNAL
amf::AMFFactory *GetFactory(GstAMFContext * amf_ctx);
G_GNUC_INTERNAL
amf::AMFTrace *GetTrace(GstAMFContext * amf_ctx);
G_GNUC_INTERNAL
uint64_t GetRuntimeVersion(GstAMFContext * amf_ctx);

G_GNUC_INTERNAL
AMFEncoderCaps GetH264Caps(GstAMFContext * amf_ctx, int deviceNum);
G_GNUC_INTERNAL
AMFEncoderCaps GetHEVCCaps(GstAMFContext * amf_ctx, int deviceNum);
G_GNUC_INTERNAL
int defaultDeviceH264(GstAMFContext * amf_ctx);
G_GNUC_INTERNAL
int defaultDeviceHEVC(GstAMFContext * amf_ctx);
G_GNUC_INTERNAL
bool H264Available(GstAMFContext * amf_ctx);
G_GNUC_INTERNAL
bool HEVCAvailable(GstAMFContext * amf_ctx);

G_GNUC_INTERNAL
gboolean gst_amf_ensure_element_context (GstElement * element, GstAMFContext ** amf_ctx);

G_END_DECLS

#endif /* __GST_AMF_CONTEXT_H__ */
