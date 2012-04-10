plugin_LTLIBRARIES = libgstuvch264.la

libgstuvch264_la_SOURCES = gstuvch264.c \
			   gstuvch264_mjpgdemux.c \
			   gstuvch264_src.c

libgstuvch264_la_CFLAGS =   $(GST_PLUGINS_BASE_CFLAGS) \
	  		    $(GST_BASE_CFLAGS) \
			    $(GST_CFLAGS)

libgstuvch264_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstuvch264_la_LIBTOOLFLAGS = --tag=disable-static

libgstuvch264_la_LIBADD =   $(GST_PLUGINS_BASE_LIBS) \
			    $(GST_BASE_LIBS) \
			    $(GST_PLUGINS_BASE_LIBS) \
			    $(GST_LIBS) \
			$(top_builddir)/gst-libs/gst/basecamerabinsrc/libgstbasecamerabinsrc-$(GST_MAJORMINOR).la 

noinst_HEADERS = gstuvch264_mjpgdemux.h \
		 gstuvch264_src.h
