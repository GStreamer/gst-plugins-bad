plugin_LTLIBRARIES = libgstnvdec.la

libgstnvdec_la_SOURCES = \
	gstnvh264plugin.c \
	gstnvh264dec.c

noinst_HEADERS = \
	gstnvh264dec.h

libgstnvdec_la_CFLAGS = \
	-I$(top_srcdir)/gst-libs \
	$(GST_CFLAGS) \
	$(GST_PBUTILS_CFLAGS) \
	$(GST_VIDEO_CFLAGS) \
	$(CUDA_CFLAGS) \
	$(NVDECODE_CFLAGS)

libgstnvdec_la_LIBADD = \
	$(GST_LIBS) \
	$(GST_PBUTILS_LIBS) \
	$(GST_VIDEO_LIBS) \
	$(CUDA_LIBS) \
	$(NVDECODE_LIBS)

libgstnvdec_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstnvdec_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)
