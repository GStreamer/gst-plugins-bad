if USE_GCONFTOOL
GCONF_DIR=gconf
else
GCONF_DIR=
endif

if USE_GCONF
GCONF_PC=gstreamer-gconf-@GST_MAJORMINOR@.pc
GCONF_PC_UNINSTALLED=gstreamer-gconf-@GST_MAJORMINOR@-uninstalled.pc
else
GCONF_PC=
GCONF_PC_UNINSTALLED=
endif

if BUILD_EXTERNAL
EXT_DIR=ext
else
EXT_DIR=
endif

if BUILD_EXAMPLES
EXAMPLES_DIR=examples
else
EXAMPLES_DIR=
endif

### all of the standard pc files we need to generate
pcfiles =  \
	gstreamer-libs-@GST_MAJORMINOR@.pc \
	gstreamer-play-@GST_MAJORMINOR@.pc
pcfiles_uninstalled = \
	gstreamer-libs-@GST_MAJORMINOR@-uninstalled.pc \
	gstreamer-play-@GST_MAJORMINOR@-uninstalled.pc

all-local: $(pcfiles) $(pcfiles_uninstalled) \
	$(GCONF_PC) \
	$(GCONF_PC_UNINSTALLED) \
	gst-element-check-@GST_MAJORMINOR@.m4

### how to generate pc files
$(pcfiles): %-@GST_MAJORMINOR@.pc: %.pc
	cp $< $@
$(pcfiles_uninstalled): %-@GST_MAJORMINOR@-uninstalled.pc: %-uninstalled.pc
	cp $< $@

gstreamer-gconf-@GST_MAJORMINOR@.pc: $(srcdir)/gst-libs/gst/gconf/gstreamer-gconf.pc
	cp $< $@
gstreamer-gconf-@GST_MAJORMINOR@-uninstalled.pc: \
	$(srcdir)/gst-libs/gst/gconf/gstreamer-gconf-uninstalled.pc
	cp $< $@

gst-element-check-@GST_MAJORMINOR@.m4: gst-element-check.m4
	cp $(srcdir)/gst-element-check.m4 gst-element-check-@GST_MAJORMINOR@.m4

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = \
	$(GCONF_PC) \
	gstreamer-libs-@GST_MAJORMINOR@.pc \
	gstreamer-play-@GST_MAJORMINOR@.pc

aclocaldir = $(datadir)/aclocal
aclocal_DATA = gst-element-check-@GST_MAJORMINOR@.m4

SUBDIRS=gst-libs gst sys $(EXT_DIR) $(EXAMPLES_DIR) tools $(GCONF_DIR) testsuite

DIST_SUBDIRS=gst-libs gst sys ext examples tools gconf testsuite

EXTRA_DIST=gst-plugins.spec depcomp gst-element-check.m4 \
           gstreamer-libs.pc.in gstreamer-libs-uninstalled.pc.in \
           gstreamer-play.pc.in gstreamer-play-uninstalled.pc.in \
	   gstreamer-gconf.pc.in gstreamer-gconf.pc-uninstalled.in \
	   AUTHORS COPYING README RELEASE REQUIREMENTS \
	   ChangeLog autogen.sh common m4

dist-hook:
	rm -rf `find $(distdir)/common -name CVS`
	rm -rf `find $(distdir)/m4 -name CVS`
