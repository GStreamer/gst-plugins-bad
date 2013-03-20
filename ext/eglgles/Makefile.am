plugin_LTLIBRARIES = libgsteglglessink.la

if HAVE_IOS
DISTRO_SRC = gstegladaptation_eagl.m
else
DISTRO_SRC = gstegladaptation_egl.c video_platform_wrapper.c
endif

libgsteglglessink_la_SOURCES = gsteglglessink.c gstegladaptation.c $(DISTRO_SRC)

libgsteglglessink_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) \
                              $(GST_BASE_CFLAGS) \
                              $(GST_CFLAGS) \
                              $(EGLGLES_CFLAGS)
libgsteglglessink_la_CPPFLAGS = $(libgsteglglessink_la_CFLAGS)
libgsteglglessink_la_OBJCFLAGS = $(libgsteglglessink_la_CFLAGS)

libgsteglglessink_la_LIBADD = $(GST_LIBS) $(GST_BASE_LIBS) \
	$(GST_PLUGINS_BASE_LIBS) $(EGLGLES_LIBS) \
	-lgstvideo-$(GST_MAJORMINOR) \
	-lgstinterfaces-$(GST_MAJORMINOR)

if HAVE_IOS
libgsteglglessink_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) -Wl,-framework,OpenGLES -Wl,-framework,QuartzCore -Wl,-framework,UIKit -Wl,-framework,Foundation
else
libgsteglglessink_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
endif
if !GST_PLUGIN_BUILD_STATIC
libgsteglglessink_la_LIBTOOLFLAGS = --tag=disable-static
endif

noinst_HEADERS = gsteglglessink.h video_platform_wrapper.h gstegladaptation.h
