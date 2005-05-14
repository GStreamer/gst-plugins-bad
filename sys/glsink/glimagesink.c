/* GStreamer
 * Copyright (C) 2003 Julien Moutte <julien@moutte.net>
 * Copyright (C) 2005 David A. Schleef <ds@schleef.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#define ENABLE_YUV

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

/* Our interfaces */
#include <gst/navigation/navigation.h>
#include <gst/xoverlay/xoverlay.h>
#include <gst/video/video.h>

/* Object header */
#include "glimagesink.h"

/* Debugging category */
#include <gst/gstinfo.h>
GST_DEBUG_CATEGORY_STATIC (gst_debug_glimagesink);
#define GST_CAT_DEFAULT gst_debug_glimagesink

static void gst_glimagesink_set_window_size (GstGLImageSink * glimagesink,
    int width, int height);
static void gst_glimagesink_create_window (GstGLImageSink * glimagesink);


static GstElementDetails gst_glimagesink_details =
GST_ELEMENT_DETAILS ("Video sink",
    "Sink/Video",
    "An OpenGL 1.2 based videosink",
    "Gernot Ziegler <gz@lysator.liu.se>, Julien Moutte <julien@moutte.net>");

/* Default template - initiated with class struct to allow gst-register to work
   without X running */
static GstStaticPadTemplate gst_glimagesink_sink_template_factory =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_RGBx ";" GST_VIDEO_CAPS_BGRx
#ifdef ENABLE_YUV
        ";" GST_VIDEO_CAPS_YUV ("{ UYVY, YUY2 }")
#endif
    )
    );

#if 0
enum
{
  LAST_SIGNAL
};

static guint gst_glimagesink_signals[LAST_SIGNAL] = { 0 };
#endif

enum
{
  ARG_0,
  ARG_DISPLAY
};

static GstVideoSinkClass *parent_class = NULL;

/* 
=================
Element stuff 
=================
*/

static GstCaps *
gst_glimagesink_fixate (GstPad * pad, const GstCaps * caps)
{
  GstStructure *structure;
  GstCaps *newcaps;

  GST_DEBUG ("Linking the sink");

  if (gst_caps_get_size (caps) > 1) {
    int i;

    for (i = 0; i < gst_caps_get_size (caps); i++) {
      structure = gst_caps_get_structure (caps, i);
      if (strcmp (gst_structure_get_name (structure), "video/x-raw-yuv") == 0) {
        newcaps = gst_caps_new_empty ();
        gst_caps_append_structure (newcaps, gst_structure_copy (structure));
        return newcaps;
      }
    }
    return NULL;
  }

  newcaps = gst_caps_copy (caps);
  structure = gst_caps_get_structure (newcaps, 0);

  if (gst_caps_structure_fixate_field_nearest_int (structure, "width", 320)) {
    return newcaps;
  }
  if (gst_caps_structure_fixate_field_nearest_int (structure, "height", 240)) {
    return newcaps;
  }
  if (gst_caps_structure_fixate_field_nearest_double (structure, "framerate",
          30.0)) {
    return newcaps;
  }

  gst_caps_free (newcaps);
  return NULL;
}

static void
gst_caps_set_all (GstCaps * caps, char *field, ...)
{
  GstStructure *structure;
  va_list var_args;
  int i;

  for (i = 0; i < gst_caps_get_size (caps); i++) {
    structure = gst_caps_get_structure (caps, i);

    va_start (var_args, field);
    gst_structure_set_valist (structure, field, var_args);
    va_end (var_args);
  }
}

static GstCaps *
gst_glimagesink_getcaps (GstPad * pad)
{
  GstGLImageSink *glimagesink;
  GstCaps *caps;

  glimagesink = GST_GLIMAGESINK (gst_pad_get_parent (pad));

  if (glimagesink->display == NULL) {
    return gst_caps_copy (gst_pad_get_pad_template_caps (pad));
  }

  caps = gst_caps_from_string (GST_VIDEO_CAPS_RGBx ";" GST_VIDEO_CAPS_BGRx);
#ifdef ENABLE_YUV
  if (glimagesink->have_yuv) {
    GstCaps *ycaps =
        gst_caps_from_string (GST_VIDEO_CAPS_YUV ("{ UYVY, YUY2 }"));
    gst_caps_append (ycaps, caps);
    caps = ycaps;
  }
#endif

  gst_caps_set_all (caps,
      "width", GST_TYPE_INT_RANGE, 16, glimagesink->max_texture_size,
      "height", GST_TYPE_INT_RANGE, 16, glimagesink->max_texture_size, NULL);

  return caps;
}

static GstPadLinkReturn
gst_glimagesink_sink_link (GstPad * pad, const GstCaps * caps)
{
  GstGLImageSink *glimagesink;
  gboolean ret;
  GstStructure *structure;

  glimagesink = GST_GLIMAGESINK (gst_pad_get_parent (pad));

  structure = gst_caps_get_structure (caps, 0);
  ret = gst_structure_get_int (structure, "width",
      &(GST_VIDEOSINK_WIDTH (glimagesink)));
  ret &= gst_structure_get_int (structure, "height",
      &(GST_VIDEOSINK_HEIGHT (glimagesink)));
  ret &= gst_structure_get_double (structure,
      "framerate", &glimagesink->framerate);
  if (!ret)
    return GST_PAD_LINK_REFUSED;

  glimagesink->pixel_width = 1;
  gst_structure_get_int (structure, "pixel_width", &glimagesink->pixel_width);

  glimagesink->pixel_height = 1;
  gst_structure_get_int (structure, "pixel_height", &glimagesink->pixel_height);

  if (strcmp (gst_structure_get_name (structure), "video/x-raw-rgb") == 0) {
    int red_mask;

    GST_DEBUG ("using RGB");
    glimagesink->use_rgb = TRUE;
    gst_structure_get_int (structure, "red_mask", &red_mask);

    if (red_mask == 0xff000000) {
      glimagesink->use_rgbx = TRUE;
    } else {
      glimagesink->use_rgbx = FALSE;
    }
  } else {
    unsigned int fourcc;

    GST_DEBUG ("using YUV");
    glimagesink->use_rgb = FALSE;

    gst_structure_get_fourcc (structure, "format", &fourcc);
    if (fourcc == GST_MAKE_FOURCC ('Y', 'U', 'Y', '2')) {
      glimagesink->use_yuy2 = TRUE;
    } else {
      glimagesink->use_yuy2 = FALSE;
    }
  }

  gst_glimagesink_set_window_size (glimagesink,
      GST_VIDEOSINK_WIDTH (glimagesink), GST_VIDEOSINK_HEIGHT (glimagesink));

  gst_x_overlay_got_desired_size (GST_X_OVERLAY (glimagesink),
      GST_VIDEOSINK_WIDTH (glimagesink), GST_VIDEOSINK_HEIGHT (glimagesink));

  return GST_PAD_LINK_OK;
}

static gboolean
gst_glimagesink_init_display (GstGLImageSink * glimagesink)
{
  gboolean ret;
  XVisualInfo *visinfo;
  Screen *screen;
  Window root;
  int scrnum;
  int attrib[] = { GLX_RGBA, GLX_DOUBLEBUFFER, GLX_RED_SIZE, 8,
    GLX_GREEN_SIZE, 8, GLX_BLUE_SIZE, 8, None
  };
  XSetWindowAttributes attr;
  int error_base;
  int event_base;
  int mask;
  const char *extstring;
  Window window;

  glimagesink->display = XOpenDisplay (NULL);
  if (glimagesink->display == NULL) {
    GST_ERROR ("Could not open display");
    return FALSE;
  }

  screen = XDefaultScreenOfDisplay (glimagesink->display);
  scrnum = XScreenNumberOfScreen (screen);
  root = XRootWindow (glimagesink->display, scrnum);

  ret = glXQueryExtension (glimagesink->display, &error_base, &event_base);
  if (!ret) {
    GST_ERROR ("No GLX extension");
    return FALSE;
  }

  visinfo = glXChooseVisual (glimagesink->display, scrnum, attrib);
  if (visinfo == NULL) {
    GST_ERROR ("No usable visual");
    return FALSE;
  }

  glimagesink->visinfo = visinfo;

  glimagesink->context = glXCreateContext (glimagesink->display,
      visinfo, NULL, True);

  attr.background_pixel = 0;
  attr.border_pixel = 0;
  attr.colormap = XCreateColormap (glimagesink->display, root,
      visinfo->visual, AllocNone);
  attr.event_mask = StructureNotifyMask | ExposureMask;
  attr.override_redirect = True;

  //mask = CWBackPixel | CWBorderPixel | CWColormap | CWEventMask;
  mask = CWBackPixel | CWBorderPixel | CWColormap | CWOverrideRedirect;

  window = XCreateWindow (glimagesink->display, root, 0, 0,
      100, 100, 0, visinfo->depth, InputOutput, visinfo->visual, mask, &attr);

  glXMakeCurrent (glimagesink->display, window, glimagesink->context);

  glGetIntegerv (GL_MAX_TEXTURE_SIZE, &glimagesink->max_texture_size);

  extstring = (const char *) glGetString (GL_EXTENSIONS);
  if (strstr (extstring, "GL_MESA_ycbcr_texture")) {
    glimagesink->have_yuv = TRUE;
  } else {
    glimagesink->have_yuv = FALSE;
  }

  glXMakeCurrent (glimagesink->display, None, NULL);
  XDestroyWindow (glimagesink->display, window);

  return TRUE;
}

static void
gst_glimagesink_create_window (GstGLImageSink * glimagesink)
{
  gboolean ret;
  Window root;
  XSetWindowAttributes attr;
  Screen *screen;
  int scrnum;
  int mask;
  int width, height;

  screen = XDefaultScreenOfDisplay (glimagesink->display);
  scrnum = XScreenNumberOfScreen (screen);
  root = XRootWindow (glimagesink->display, scrnum);

  if (glimagesink->parent_window) {
    XWindowAttributes pattr;

    XGetWindowAttributes (glimagesink->display, glimagesink->parent_window,
        &pattr);
    width = pattr.width;
    height = pattr.height;
  } else {
    width = GST_VIDEOSINK (glimagesink)->width;
    height = GST_VIDEOSINK (glimagesink)->height;
  }
  attr.background_pixel = 0;
  attr.border_pixel = 0;
  attr.colormap = XCreateColormap (glimagesink->display, root,
      glimagesink->visinfo->visual, AllocNone);
  if (glimagesink->parent_window) {
    attr.override_redirect = True;
  } else {
    attr.override_redirect = False;
  }

  mask = CWBackPixel | CWBorderPixel | CWColormap | CWOverrideRedirect;

  glimagesink->window = XCreateWindow (glimagesink->display, root, 0, 0,
      width, height,
      0, glimagesink->visinfo->depth, InputOutput,
      glimagesink->visinfo->visual, mask, &attr);

  if (glimagesink->parent_window) {
    ret = XReparentWindow (glimagesink->display, glimagesink->window,
        glimagesink->parent_window, 0, 0);
    XMapWindow (glimagesink->display, glimagesink->window);
  } else {
    XMapWindow (glimagesink->display, glimagesink->window);
  }

  glXMakeCurrent (glimagesink->display, glimagesink->window,
      glimagesink->context);

  glDepthFunc (GL_LESS);
  glEnable (GL_DEPTH_TEST);
  glClearColor (0.2, 0.2, 0.2, 1.0);
  glViewport (0, 0, width, height);
}

static void
gst_glimagesink_set_window_size (GstGLImageSink * glimagesink,
    int width, int height)
{
  GST_DEBUG ("resizing to %d x %d",
      GST_VIDEOSINK_WIDTH (glimagesink), GST_VIDEOSINK_HEIGHT (glimagesink));

  if (glimagesink->display && glimagesink->window) {
    XResizeWindow (glimagesink->display, glimagesink->window, width, height);
    XSync (glimagesink->display, False);
    glViewport (0, 0, width, height);
  }
}

static GstElementStateReturn
gst_glimagesink_change_state (GstElement * element)
{
  GstGLImageSink *glimagesink;

  GST_DEBUG ("change state");

  glimagesink = GST_GLIMAGESINK (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      if (!gst_glimagesink_init_display (glimagesink)) {
        GST_ELEMENT_ERROR (glimagesink, RESOURCE, WRITE, (NULL),
            ("Could not initialize OpenGL"));
        return GST_STATE_FAILURE;
      }
      break;
    case GST_STATE_READY_TO_PAUSED:
      GST_DEBUG ("ready to paused");
      glimagesink->time = 0;
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      if (!glimagesink->window) {
        gst_glimagesink_create_window (glimagesink);
      }
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_READY:
      glimagesink->framerate = 0;
      GST_VIDEOSINK_WIDTH (glimagesink) = 0;
      GST_VIDEOSINK_HEIGHT (glimagesink) = 0;
      break;
    case GST_STATE_READY_TO_NULL:
      /* FIXME dispose of window */
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}

static void
gst_glimagesink_chain (GstPad * pad, GstData * data)
{
  GstBuffer *buf = GST_BUFFER (data);
  GstGLImageSink *glimagesink;
  int texture_size;
  XWindowAttributes attr;

  //GST_DEBUG("CHAIN CALL");

  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  glimagesink = GST_GLIMAGESINK (gst_pad_get_parent (pad));

  if (glimagesink->display == NULL || glimagesink->window == 0) {
    g_warning ("display or window not set up\n");
  }

  if (GST_IS_EVENT (data)) {
    gst_pad_event_default (pad, GST_EVENT (data));
    return;
  }

  buf = GST_BUFFER (data);
  /* update time */
  if (GST_BUFFER_TIMESTAMP_IS_VALID (buf)) {
    glimagesink->time = GST_BUFFER_TIMESTAMP (buf);
  }

  glXMakeCurrent (glimagesink->display, glimagesink->window,
      glimagesink->context);

  if (glimagesink->parent_window) {
    XGetWindowAttributes (glimagesink->display, glimagesink->parent_window,
        &attr);
    gst_glimagesink_set_window_size (glimagesink, attr.width, attr.height);
  } else {
    XGetWindowAttributes (glimagesink->display, glimagesink->window, &attr);
    glViewport (0, 0, attr.width, attr.height);
  }

  GST_DEBUG ("clock wait: %" GST_TIME_FORMAT,
      GST_TIME_ARGS (glimagesink->time));

  /// ah, BTW, I think the gst_element_wait should happen _before_ the ximage is shown
  if (GST_VIDEOSINK_CLOCK (glimagesink))
    gst_element_wait (GST_ELEMENT (glimagesink), glimagesink->time);

  /* set correct time for next buffer */
  if (!GST_BUFFER_TIMESTAMP_IS_VALID (buf) && glimagesink->framerate > 0)
    glimagesink->time += GST_SECOND / glimagesink->framerate;

  glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  glMatrixMode (GL_PROJECTION);
  glLoadIdentity ();

  glMatrixMode (GL_MODELVIEW);
  glLoadIdentity ();

  glDisable (GL_CULL_FACE);
  glEnable (GL_TEXTURE_2D);
  glEnableClientState (GL_TEXTURE_COORD_ARRAY);

  glColor4f (1, 1, 1, 1);

#define TEXID 1000
  glBindTexture (GL_TEXTURE_2D, TEXID);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  glTexEnvi (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);

  for (texture_size = 64;
      (texture_size < GST_VIDEOSINK (glimagesink)->width ||
          texture_size < GST_VIDEOSINK (glimagesink)->height) &&
      (texture_size > 0); texture_size <<= 1);

  if (glimagesink->use_rgb) {
    glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA, texture_size,
        texture_size, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

    if (glimagesink->use_rgbx) {
      glTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0,
          GST_VIDEOSINK (glimagesink)->width,
          GST_VIDEOSINK (glimagesink)->height,
          GL_RGBA, GL_UNSIGNED_BYTE, GST_BUFFER_DATA (buf));
    } else {
      glTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0,
          GST_VIDEOSINK (glimagesink)->width,
          GST_VIDEOSINK (glimagesink)->height,
          GL_BGRA, GL_UNSIGNED_BYTE, GST_BUFFER_DATA (buf));
    }
  } else {
    glTexImage2D (GL_TEXTURE_2D, 0, GL_YCBCR_MESA, texture_size,
        texture_size, 0, GL_YCBCR_MESA, GL_UNSIGNED_SHORT_8_8_REV_MESA, NULL);

    if (glimagesink->use_yuy2) {
      glTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0,
          GST_VIDEOSINK (glimagesink)->width,
          GST_VIDEOSINK (glimagesink)->height,
          GL_YCBCR_MESA, GL_UNSIGNED_SHORT_8_8_REV_MESA, GST_BUFFER_DATA (buf));
    } else {
      glTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0,
          GST_VIDEOSINK (glimagesink)->width,
          GST_VIDEOSINK (glimagesink)->height,
          GL_YCBCR_MESA, GL_UNSIGNED_SHORT_8_8_MESA, GST_BUFFER_DATA (buf));
    }
  }

  glColor4f (1, 0, 1, 1);
  glBegin (GL_QUADS);

  glNormal3f (0, 0, -1);

  {
    double xmax = GST_VIDEOSINK (glimagesink)->width / (double) texture_size;
    double ymax = GST_VIDEOSINK (glimagesink)->height / (double) texture_size;

    glTexCoord2f (xmax, 0);
    glVertex3f (1.0, 1.0, 0);
    glTexCoord2f (0, 0);
    glVertex3f (-1.0, 1.0, 0);
    glTexCoord2f (0, ymax);
    glVertex3f (-1.0, -1.0, 0);
    glTexCoord2f (xmax, ymax);
    glVertex3f (1.0, -1.0, 0);
    glEnd ();
  }

  glFlush ();
  glXSwapBuffers (glimagesink->display, glimagesink->window);

  gst_buffer_unref (buf);
}

#if 0
static void
gst_glimagesink_navigation_send_event (GstNavigation * navigation,
    GstStructure * structure)
{
  GstGLImageSink *glimagesink = GST_GLIMAGESINK (navigation);
  GstEvent *event;
  gint x_offset, y_offset;
  double x, y;

  event = gst_event_new (GST_EVENT_NAVIGATION);
  event->event_data.structure.structure = structure;

  /* We are not converting the pointer coordinates as there's no hardware 
     scaling done here. The only possible scaling is done by videoscale and
     videoscale will have to catch those events and tranform the coordinates
     to match the applied scaling. So here we just add the offset if the image
     is centered in the window.  */

  x_offset = glimagesink->window->width - GST_VIDEOSINK_WIDTH (glimagesink);
  y_offset = glimagesink->window->height - GST_VIDEOSINK_HEIGHT (glimagesink);

  if (gst_structure_get_double (structure, "pointer_x", &x)) {
    x += x_offset;
    gst_structure_set (structure, "pointer_x", G_TYPE_DOUBLE, x, NULL);
  }
  if (gst_structure_get_double (structure, "pointer_y", &y)) {
    y += y_offset;
    gst_structure_set (structure, "pointer_y", G_TYPE_DOUBLE, y, NULL);
  }

  gst_pad_send_event (gst_pad_get_peer (GST_VIDEOSINK_PAD (glimagesink)),
      event);
}

static void
gst_glimagesink_navigation_init (GstNavigationInterface * iface)
{
  iface->send_event = gst_glimagesink_navigation_send_event;
}
#endif

static void
gst_glimagesink_set_xwindow_id (GstXOverlay * overlay, XID xwindow_id)
{
  GstGLImageSink *glimagesink = GST_GLIMAGESINK (overlay);

  GST_DEBUG ("set_xwindow_id %ld", xwindow_id);

  g_return_if_fail (GST_IS_GLIMAGESINK (glimagesink));

  /* If the element has not initialized the X11 context try to do so */
  if (!glimagesink->display) {
    g_warning ("X display not inited\n");
  }

  if (glimagesink->parent_window == xwindow_id)
    return;

  glimagesink->parent_window = xwindow_id;

  XSync (glimagesink->display, False);
  gst_glimagesink_create_window (glimagesink);
}

static void
gst_glimagesink_get_desired_size (GstXOverlay * overlay,
    guint * width, guint * height)
{
  GstGLImageSink *glimagesink = GST_GLIMAGESINK (overlay);

  *width = GST_VIDEOSINK_WIDTH (glimagesink);
  *height = GST_VIDEOSINK_HEIGHT (glimagesink);
}

static void
gst_glimagesink_expose (GstXOverlay * overlay)
{
  GstGLImageSink *glimagesink = GST_GLIMAGESINK (overlay);

  if (!glimagesink->display)
    return;

  /* Don't need to do anything */
}

static void
gst_glimagesink_xoverlay_init (GstXOverlayClass * iface)
{
  iface->set_xwindow_id = gst_glimagesink_set_xwindow_id;
  iface->get_desired_size = gst_glimagesink_get_desired_size;
  iface->expose = gst_glimagesink_expose;
}

/* =========================================== */
/*                                             */
/*              Init & Class init              */
/*                                             */
/* =========================================== */

static void
gst_glimagesink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstGLImageSink *glimagesink;

  g_return_if_fail (GST_IS_GLIMAGESINK (object));

  glimagesink = GST_GLIMAGESINK (object);

  switch (prop_id) {
    case ARG_DISPLAY:
      glimagesink->display_name = g_strdup (g_value_get_string (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_glimagesink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstGLImageSink *glimagesink;

  g_return_if_fail (GST_IS_GLIMAGESINK (object));

  glimagesink = GST_GLIMAGESINK (object);

  switch (prop_id) {
    case ARG_DISPLAY:
      g_value_set_string (value, g_strdup (glimagesink->display_name));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_glimagesink_finalize (GObject * object)
{
  GstGLImageSink *glimagesink;

  glimagesink = GST_GLIMAGESINK (object);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_glimagesink_init (GstGLImageSink * glimagesink)
{
  GST_VIDEOSINK_PAD (glimagesink) =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_glimagesink_sink_template_factory), "sink");

  gst_element_add_pad (GST_ELEMENT (glimagesink),
      GST_VIDEOSINK_PAD (glimagesink));

  gst_pad_set_chain_function (GST_VIDEOSINK_PAD (glimagesink),
      gst_glimagesink_chain);
  gst_pad_set_link_function (GST_VIDEOSINK_PAD (glimagesink),
      gst_glimagesink_sink_link);
  gst_pad_set_getcaps_function (GST_VIDEOSINK_PAD (glimagesink),
      gst_glimagesink_getcaps);
  gst_pad_set_fixate_function (GST_VIDEOSINK_PAD (glimagesink),
      gst_glimagesink_fixate);

  glimagesink->pixel_width = 1;
  glimagesink->pixel_height = 1;
  GST_VIDEOSINK_WIDTH (glimagesink) = 100;
  GST_VIDEOSINK_HEIGHT (glimagesink) = 100;

  GST_FLAG_SET (glimagesink, GST_ELEMENT_THREAD_SUGGESTED);
  GST_FLAG_SET (glimagesink, GST_ELEMENT_EVENT_AWARE);
}

static void
gst_glimagesink_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (element_class, &gst_glimagesink_details);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_glimagesink_sink_template_factory));
}

static void
gst_glimagesink_class_init (GstGLImageSinkClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_VIDEOSINK);

  g_object_class_install_property (gobject_class, ARG_DISPLAY,
      g_param_spec_string ("display", "Display", "X Display name",
          NULL, G_PARAM_READWRITE));

  gobject_class->finalize = gst_glimagesink_finalize;
  gobject_class->set_property = gst_glimagesink_set_property;
  gobject_class->get_property = gst_glimagesink_get_property;

  gstelement_class->change_state = gst_glimagesink_change_state;
}

static gboolean
gst_glimagesink_interface_supported (GstImplementsInterface * iface, GType type)
{
  return TRUE;
}

static void
gst_glimagesink_interface_init (GstImplementsInterfaceClass * klass)
{
  klass->supported = gst_glimagesink_interface_supported;
}

/* ============================================================= */
/*                                                               */
/*                       Public Methods                          */
/*                                                               */
/* ============================================================= */

/* =========================================== */
/*                                             */
/*          Object typing & Creation           */
/*                                             */
/* =========================================== */

GType
gst_glimagesink_get_type (void)
{
  static GType glimagesink_type = 0;

  if (!glimagesink_type) {
    static const GTypeInfo glimagesink_info = {
      sizeof (GstGLImageSinkClass),
      gst_glimagesink_base_init,
      NULL,
      (GClassInitFunc) gst_glimagesink_class_init,
      NULL,
      NULL,
      sizeof (GstGLImageSink),
      0,
      (GInstanceInitFunc) gst_glimagesink_init,
    };
    static const GInterfaceInfo iface_info = {
      (GInterfaceInitFunc) gst_glimagesink_interface_init,
      NULL,
      NULL,
    };
#if 0
    static const GInterfaceInfo navigation_info = {
      (GInterfaceInitFunc) gst_glimagesink_navigation_init,
      NULL,
      NULL,
    };
#endif
    static const GInterfaceInfo overlay_info = {
      (GInterfaceInitFunc) gst_glimagesink_xoverlay_init,
      NULL,
      NULL,
    };

    glimagesink_type = g_type_register_static (GST_TYPE_VIDEOSINK,
        "GstGLImageSink", &glimagesink_info, 0);

    g_type_add_interface_static (glimagesink_type,
        GST_TYPE_IMPLEMENTS_INTERFACE, &iface_info);
#if 0
    g_type_add_interface_static (glimagesink_type, GST_TYPE_NAVIGATION,
        &navigation_info);
#endif
    g_type_add_interface_static (glimagesink_type, GST_TYPE_X_OVERLAY,
        &overlay_info);
  }

  return glimagesink_type;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  /* Loading the library containing GstVideoSink, our parent object */
  if (!gst_library_load ("gstvideo"))
    return FALSE;

  if (!gst_element_register (plugin, "glimagesink",
          GST_RANK_PRIMARY + 1, GST_TYPE_GLIMAGESINK))
    return FALSE;

  GST_DEBUG_CATEGORY_INIT (gst_debug_glimagesink, "glimagesink", 0,
      "glimagesink element");

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "glimagesink",
    "OpenGL video output plugin based on OpenGL 1.2 calls",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE, GST_ORIGIN)
