/*
 * GStreamer EGL/GLES Sink
 * Copyright (C) 2012 Collabora Ltd.
 *   @author: Reynaldo H. Verdejo Pinochet <reynaldo@collabora.com>
 *   @author: Sebastian Dröge <sebastian.droege@collabora.co.uk>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Alternatively, the contents of this file may be used under the
 * GNU Lesser General Public License Version 2.1 (the "LGPL"), in
 * which case the following provisions apply instead of the ones
 * mentioned above:
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

/**
 * SECTION:element-eglglessink
 *
 * EglGlesSink renders video frames on a EGL surface it sets up
 * from a window it either creates (on X11) or gets a handle to
 * through it's xOverlay interface. All the display/surface logic
 * in this sink uses EGL to interact with the native window system.
 * The rendering logic, in turn, uses OpenGL ES v2.
 *
 * This sink has been tested to work on X11/Mesa and on Android
 * (From Gingerbread on to Jelly Bean) and while it's currently
 * using an slow copy-over rendering path it has proven to be fast
 * enough on the devices we have tried it on. 
 *
 * <refsect2>
 * <title>Supported EGL/OpenGL ES versions</title>
 * <para>
 * This Sink uses EGLv1 and GLESv2
 * </para>
 * </refsect2>
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v -m videotestsrc ! eglglessink
 * ]|
 * </refsect2>
 *
 * <refsect2>
 * <title>Example launch line with internal window creation disabled</title>
 * <para>
 * By setting the can_create_window property to FALSE you can force the
 * sink to wait for a window handle through it's xOverlay interface even
 * if internal window creation is supported by the platform. Window creation
 * is only supported in X11 right now but it should be trivial to add support
 * for different platforms.
 * </para>
 * |[
 * gst-launch -v -m videotestsrc ! eglglessink can_create_window=FALSE
 * ]|
 * </refsect2>
 *
 * <refsect2>
 * <title>Scaling</title>
 * <para>
 * The sink will try it's best to consider the incoming frame's and display's
 * pixel aspect ratio and fill the corresponding surface without altering the
 * decoded frame's geometry when scaling. You can disable this logic by setting
 * the force_aspect_ratio property to FALSE, in which case the sink will just
 * fill the entire surface it has access to regardles of the PAR/DAR relationship.
 * </para>
 * <para>
 * Querying the display aspect ratio is only supported with EGL versions >= 1.2.
 * The sink will just assume the DAR to be 1/1 if it can't get access to this
 * information.
 * </para>
 * <para>
 * Here is an example launch line with the PAR/DAR aware scaling disabled:
 * </para>
 * |[
 * gst-launch -v -m videotestsrc ! eglglessink force_aspect_ratio=FALSE
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <string.h>
#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideosink.h>
#include <gst/interfaces/xoverlay.h>

#ifdef USE_EGL_RPI
#include <bcm_host.h>
#endif

#include "gsteglglessink.h"

GST_DEBUG_CATEGORY_STATIC (gst_eglglessink_debug);
#define GST_CAT_DEFAULT gst_eglglessink_debug

/* Input capabilities. */
static GstStaticPadTemplate gst_eglglessink_sink_template_factory =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_RGBA ";" GST_VIDEO_CAPS_BGRA ";"
        GST_VIDEO_CAPS_ARGB ";" GST_VIDEO_CAPS_ABGR ";"
        GST_VIDEO_CAPS_RGBx ";" GST_VIDEO_CAPS_BGRx ";"
        GST_VIDEO_CAPS_xRGB ";" GST_VIDEO_CAPS_xBGR ";"
        GST_VIDEO_CAPS_YUV
        ("{ AYUV, Y444, I420, YV12, NV12, NV21, YUY2, YVYU, UYVY, Y42B, Y41B }")
        ";" GST_VIDEO_CAPS_RGB ";" GST_VIDEO_CAPS_BGR ";"
        GST_VIDEO_CAPS_RGB_16));

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_CREATE_WINDOW,
  PROP_FORCE_ASPECT_RATIO,
};

static void gst_eglglessink_finalize (GObject * object);
static void gst_eglglessink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_eglglessink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static GstStateChangeReturn gst_eglglessink_change_state (GstElement * element,
    GstStateChange transition);
static GstFlowReturn gst_eglglessink_show_frame (GstVideoSink * vsink,
    GstBuffer * buf);
static gboolean gst_eglglessink_setcaps (GstBaseSink * bsink, GstCaps * caps);
static GstCaps *gst_eglglessink_getcaps (GstBaseSink * bsink);

/* XOverlay interface cruft */
static gboolean gst_eglglessink_interface_supported
    (GstImplementsInterface * iface, GType type);
static void gst_eglglessink_implements_init
    (GstImplementsInterfaceClass * klass);
static void gst_eglglessink_xoverlay_init (GstXOverlayClass * iface);
static void gst_eglglessink_init_interfaces (GType type);

/* Actual XOverlay interface funcs */
static void gst_eglglessink_expose (GstXOverlay * overlay);
static void gst_eglglessink_set_window_handle (GstXOverlay * overlay,
    guintptr id);
static void gst_eglglessink_set_render_rectangle (GstXOverlay * overlay, gint x,
    gint y, gint width, gint height);

/* Utility */
static gboolean gst_eglglessink_create_window (GstEglGlesSink *
    eglglessink, gint width, gint height);
static gboolean gst_eglglessink_setup_vbo (GstEglGlesSink * eglglessink,
    gboolean reset);
static gboolean
gst_eglglessink_configure_caps (GstEglGlesSink * eglglessink, GstCaps * caps);
static GstFlowReturn gst_eglglessink_upload (GstEglGlesSink * sink,
    GstBuffer * buf);
static GstFlowReturn gst_eglglessink_render (GstEglGlesSink * sink);
static GstFlowReturn gst_eglglessink_queue_buffer (GstEglGlesSink * sink,
    GstBuffer * buf);
static inline gboolean egl_init (GstEglGlesSink * eglglessink);

GST_BOILERPLATE_FULL (GstEglGlesSink, gst_eglglessink, GstVideoSink,
    GST_TYPE_VIDEO_SINK, gst_eglglessink_init_interfaces);

static inline gboolean
egl_init (GstEglGlesSink * eglglessink)
{
  if (!gst_egl_adaptation_init_display (eglglessink->egl_context)) {
    GST_ERROR_OBJECT (eglglessink, "Couldn't init EGL display");
    goto HANDLE_ERROR;
  }

  gst_egl_adaptation_init_egl_exts (eglglessink->egl_context);

  if (!gst_egl_adaptation_fill_supported_fbuffer_configs
      (eglglessink->egl_context, &eglglessink->sinkcaps)) {
    GST_ERROR_OBJECT (eglglessink, "Display support NONE of our configs");
    goto HANDLE_ERROR;
  }

  eglglessink->egl_started = TRUE;

  return TRUE;

HANDLE_ERROR:
  GST_ERROR_OBJECT (eglglessink, "Failed to perform EGL init");
  return FALSE;
}

static gpointer
render_thread_func (GstEglGlesSink * eglglessink)
{
  GstMessage *message;
  GValue val = { 0 };
  GstDataQueueItem *item = NULL;
  GstFlowReturn last_flow = GST_FLOW_OK;

  g_value_init (&val, G_TYPE_POINTER);
  g_value_set_pointer (&val, g_thread_self ());
  message = gst_message_new_stream_status (GST_OBJECT_CAST (eglglessink),
      GST_STREAM_STATUS_TYPE_ENTER, GST_ELEMENT_CAST (eglglessink));
  gst_message_set_stream_status_object (message, &val);
  GST_DEBUG_OBJECT (eglglessink, "posting ENTER stream status");
  gst_element_post_message (GST_ELEMENT_CAST (eglglessink), message);
  g_value_unset (&val);

  gst_egl_adaptation_bind_API (eglglessink->egl_context);

  while (gst_data_queue_pop (eglglessink->queue, &item)) {
    GstBuffer *buf = NULL;
    GstMiniObject *object = item->object;

    GST_DEBUG_OBJECT (eglglessink, "Handling object %" GST_PTR_FORMAT, object);

    if (GST_IS_BUFFER (object)) {
      GstCaps *caps;

      buf = GST_BUFFER_CAST (item->object);
      caps = GST_BUFFER_CAPS (buf);
      if (caps != eglglessink->configured_caps) {
        if (!gst_eglglessink_configure_caps (eglglessink, caps)) {
          g_mutex_lock (eglglessink->render_lock);
          eglglessink->last_flow = GST_FLOW_NOT_NEGOTIATED;
          g_cond_broadcast (eglglessink->render_cond);
          g_mutex_unlock (eglglessink->render_lock);
          item->destroy (item);
          break;
        }
      }
      if (eglglessink->configured_caps) {
        last_flow = gst_eglglessink_upload (eglglessink, buf);
        if (last_flow == GST_FLOW_OK) {
          last_flow = gst_eglglessink_render (eglglessink);
        }
      } else {
        GST_DEBUG_OBJECT (eglglessink,
            "No caps configured yet, not drawing anything");
      }
    } else if (!object) {
      if (eglglessink->configured_caps) {
        last_flow = gst_eglglessink_render (eglglessink);
      } else {
        last_flow = GST_FLOW_OK;
        GST_DEBUG_OBJECT (eglglessink,
            "No caps configured yet, not drawing anything");
      }
    } else {
      g_assert_not_reached ();
    }

    item->destroy (item);
    g_mutex_lock (eglglessink->render_lock);
    eglglessink->last_flow = last_flow;
    g_cond_broadcast (eglglessink->render_cond);
    g_mutex_unlock (eglglessink->render_lock);

    if (last_flow != GST_FLOW_OK)
      break;
    GST_DEBUG_OBJECT (eglglessink, "Successfully handled object");
  }

  if (last_flow == GST_FLOW_OK) {
    g_mutex_lock (eglglessink->render_lock);
    eglglessink->last_flow = GST_FLOW_WRONG_STATE;
    g_cond_broadcast (eglglessink->render_cond);
    g_mutex_unlock (eglglessink->render_lock);
  }

  GST_DEBUG_OBJECT (eglglessink, "Shutting down thread");

  /* EGL/GLES cleanup */
  gst_egl_adaptation_cleanup (eglglessink->egl_context);

  if (eglglessink->configured_caps) {
    gst_caps_unref (eglglessink->configured_caps);
    eglglessink->configured_caps = NULL;
  }

  g_value_init (&val, G_TYPE_POINTER);
  g_value_set_pointer (&val, g_thread_self ());
  message = gst_message_new_stream_status (GST_OBJECT_CAST (eglglessink),
      GST_STREAM_STATUS_TYPE_LEAVE, GST_ELEMENT_CAST (eglglessink));
  gst_message_set_stream_status_object (message, &val);
  GST_DEBUG_OBJECT (eglglessink, "posting LEAVE stream status");
  gst_element_post_message (GST_ELEMENT_CAST (eglglessink), message);
  g_value_unset (&val);

  return NULL;
}

static gboolean
gst_eglglessink_start (GstEglGlesSink * eglglessink)
{
  GError *error = NULL;

  GST_DEBUG_OBJECT (eglglessink, "Starting");

  if (!eglglessink->egl_started) {
    GST_ERROR_OBJECT (eglglessink, "EGL uninitialized. Bailing out");
    goto HANDLE_ERROR;
  }

  /* Ask for a window to render to */
  if (!eglglessink->have_window)
    gst_x_overlay_prepare_xwindow_id (GST_X_OVERLAY (eglglessink));

  if (!eglglessink->have_window && !eglglessink->create_window) {
    GST_ERROR_OBJECT (eglglessink, "Window handle unavailable and we "
        "were instructed not to create an internal one. Bailing out.");
    goto HANDLE_ERROR;
  }

  eglglessink->last_flow = GST_FLOW_OK;
  eglglessink->display_region.w = 0;
  eglglessink->display_region.h = 0;

  gst_data_queue_set_flushing (eglglessink->queue, FALSE);

#if !GLIB_CHECK_VERSION (2, 31, 0)
  eglglessink->thread =
      g_thread_create ((GThreadFunc) render_thread_func, eglglessink, TRUE,
      &error);
#else
  eglglessink->thread = g_thread_try_new ("eglglessink-render",
      (GThreadFunc) render_thread_func, eglglessink, &error);
#endif

  if (!eglglessink->thread || error != NULL)
    goto HANDLE_ERROR;

  GST_DEBUG_OBJECT (eglglessink, "Started");

  return TRUE;

HANDLE_ERROR:
  GST_ERROR_OBJECT (eglglessink, "Couldn't start");
  g_clear_error (&error);
  return FALSE;
}

static gboolean
gst_eglglessink_stop (GstEglGlesSink * eglglessink)
{
  GST_DEBUG_OBJECT (eglglessink, "Stopping");

  gst_data_queue_set_flushing (eglglessink->queue, TRUE);
  g_mutex_lock (eglglessink->render_lock);
  g_cond_broadcast (eglglessink->render_cond);
  g_mutex_unlock (eglglessink->render_lock);

  if (eglglessink->thread) {
    g_thread_join (eglglessink->thread);
    eglglessink->thread = NULL;
  }
  eglglessink->last_flow = GST_FLOW_WRONG_STATE;

  if (eglglessink->using_own_window) {
    gst_egl_adaptation_destroy_native_window (eglglessink->egl_context,
        &eglglessink->own_window_data);
    eglglessink->have_window = FALSE;
  }
  if (eglglessink->current_caps) {
    gst_caps_unref (eglglessink->current_caps);
    eglglessink->current_caps = NULL;
  }

  GST_DEBUG_OBJECT (eglglessink, "Stopped");

  return TRUE;
}

static void
gst_eglglessink_xoverlay_init (GstXOverlayClass * iface)
{
  iface->set_window_handle = gst_eglglessink_set_window_handle;
  iface->expose = gst_eglglessink_expose;
  iface->set_render_rectangle = gst_eglglessink_set_render_rectangle;
}

static gboolean
gst_eglglessink_interface_supported (GstImplementsInterface * iface, GType type)
{
  return (type == GST_TYPE_X_OVERLAY);
}

static void
gst_eglglessink_implements_init (GstImplementsInterfaceClass * klass)
{
  klass->supported = gst_eglglessink_interface_supported;
}

static gboolean
gst_eglglessink_create_window (GstEglGlesSink * eglglessink, gint width,
    gint height)
{
  gboolean ret;

  if (!eglglessink->create_window) {
    GST_ERROR_OBJECT (eglglessink, "This sink can't create a window by itself");
    return FALSE;
  } else
    GST_INFO_OBJECT (eglglessink, "Attempting internal window creation");

  ret =
      gst_egl_adaptation_create_native_window (eglglessink->egl_context, width,
      height, &eglglessink->own_window_data);

  if (!ret) {
    GST_ERROR_OBJECT (eglglessink, "Could not create window");
  }
  return ret;
}

static void
gst_eglglessink_expose (GstXOverlay * overlay)
{
  GstEglGlesSink *eglglessink;
  GstFlowReturn ret;

  eglglessink = GST_EGLGLESSINK (overlay);
  GST_DEBUG_OBJECT (eglglessink, "Expose catched, redisplay");

  /* Render from last seen buffer */
  ret = gst_eglglessink_queue_buffer (eglglessink, NULL);
  if (ret == GST_FLOW_ERROR)
    GST_ERROR_OBJECT (eglglessink, "Redisplay failed");
}

static gboolean
gst_eglglessink_setup_vbo (GstEglGlesSink * eglglessink, gboolean reset)
{
  gdouble render_width, render_height;
  gdouble x1, x2, y1, y2;

  GST_INFO_OBJECT (eglglessink, "VBO setup. have_vbo:%d, should reset %d",
      eglglessink->egl_context->have_vbo, reset);

  if (eglglessink->egl_context->have_vbo && reset) {
    glDeleteBuffers (1, &eglglessink->egl_context->position_buffer);
    glDeleteBuffers (1, &eglglessink->egl_context->index_buffer);
    eglglessink->egl_context->have_vbo = FALSE;
  }

  render_width = eglglessink->render_region.w;
  render_height = eglglessink->render_region.h;

  GST_DEBUG_OBJECT (eglglessink, "Performing VBO setup");

  x1 = (eglglessink->display_region.x / render_width) * 2.0 - 1;
  y1 = (eglglessink->display_region.y / render_height) * 2.0 - 1;
  x2 = ((eglglessink->display_region.x +
          eglglessink->display_region.w) / render_width) * 2.0 - 1;
  y2 = ((eglglessink->display_region.y +
          eglglessink->display_region.h) / render_height) * 2.0 - 1;

  eglglessink->egl_context->position_array[0].x = x2;
  eglglessink->egl_context->position_array[0].y = y2;
  eglglessink->egl_context->position_array[0].z = 0;
  eglglessink->egl_context->position_array[0].a = 1;
  eglglessink->egl_context->position_array[0].b = 0;

  eglglessink->egl_context->position_array[1].x = x2;
  eglglessink->egl_context->position_array[1].y = y1;
  eglglessink->egl_context->position_array[1].z = 0;
  eglglessink->egl_context->position_array[1].a = 1;
  eglglessink->egl_context->position_array[1].b = 1;

  eglglessink->egl_context->position_array[2].x = x1;
  eglglessink->egl_context->position_array[2].y = y2;
  eglglessink->egl_context->position_array[2].z = 0;
  eglglessink->egl_context->position_array[2].a = 0;
  eglglessink->egl_context->position_array[2].b = 0;

  eglglessink->egl_context->position_array[3].x = x1;
  eglglessink->egl_context->position_array[3].y = y1;
  eglglessink->egl_context->position_array[3].z = 0;
  eglglessink->egl_context->position_array[3].a = 0;
  eglglessink->egl_context->position_array[3].b = 1;

  if (eglglessink->display_region.x == 0) {
    /* Borders top/bottom */

    eglglessink->egl_context->position_array[4 + 0].x = 1;
    eglglessink->egl_context->position_array[4 + 0].y = 1;
    eglglessink->egl_context->position_array[4 + 0].z = 0;

    eglglessink->egl_context->position_array[4 + 1].x = x2;
    eglglessink->egl_context->position_array[4 + 1].y = y2;
    eglglessink->egl_context->position_array[4 + 1].z = 0;

    eglglessink->egl_context->position_array[4 + 2].x = -1;
    eglglessink->egl_context->position_array[4 + 2].y = 1;
    eglglessink->egl_context->position_array[4 + 2].z = 0;

    eglglessink->egl_context->position_array[4 + 3].x = x1;
    eglglessink->egl_context->position_array[4 + 3].y = y2;
    eglglessink->egl_context->position_array[4 + 3].z = 0;

    eglglessink->egl_context->position_array[8 + 0].x = 1;
    eglglessink->egl_context->position_array[8 + 0].y = y1;
    eglglessink->egl_context->position_array[8 + 0].z = 0;

    eglglessink->egl_context->position_array[8 + 1].x = 1;
    eglglessink->egl_context->position_array[8 + 1].y = -1;
    eglglessink->egl_context->position_array[8 + 1].z = 0;

    eglglessink->egl_context->position_array[8 + 2].x = x1;
    eglglessink->egl_context->position_array[8 + 2].y = y1;
    eglglessink->egl_context->position_array[8 + 2].z = 0;

    eglglessink->egl_context->position_array[8 + 3].x = -1;
    eglglessink->egl_context->position_array[8 + 3].y = -1;
    eglglessink->egl_context->position_array[8 + 3].z = 0;
  } else {
    /* Borders left/right */

    eglglessink->egl_context->position_array[4 + 0].x = x1;
    eglglessink->egl_context->position_array[4 + 0].y = 1;
    eglglessink->egl_context->position_array[4 + 0].z = 0;

    eglglessink->egl_context->position_array[4 + 1].x = x1;
    eglglessink->egl_context->position_array[4 + 1].y = -1;
    eglglessink->egl_context->position_array[4 + 1].z = 0;

    eglglessink->egl_context->position_array[4 + 2].x = -1;
    eglglessink->egl_context->position_array[4 + 2].y = 1;
    eglglessink->egl_context->position_array[4 + 2].z = 0;

    eglglessink->egl_context->position_array[4 + 3].x = -1;
    eglglessink->egl_context->position_array[4 + 3].y = -1;
    eglglessink->egl_context->position_array[4 + 3].z = 0;

    eglglessink->egl_context->position_array[8 + 0].x = 1;
    eglglessink->egl_context->position_array[8 + 0].y = 1;
    eglglessink->egl_context->position_array[8 + 0].z = 0;

    eglglessink->egl_context->position_array[8 + 1].x = 1;
    eglglessink->egl_context->position_array[8 + 1].y = -1;
    eglglessink->egl_context->position_array[8 + 1].z = 0;

    eglglessink->egl_context->position_array[8 + 2].x = x2;
    eglglessink->egl_context->position_array[8 + 2].y = y2;
    eglglessink->egl_context->position_array[8 + 2].z = 0;

    eglglessink->egl_context->position_array[8 + 3].x = x2;
    eglglessink->egl_context->position_array[8 + 3].y = -1;
    eglglessink->egl_context->position_array[8 + 3].z = 0;
  }

  eglglessink->egl_context->index_array[0] = 0;
  eglglessink->egl_context->index_array[1] = 1;
  eglglessink->egl_context->index_array[2] = 2;
  eglglessink->egl_context->index_array[3] = 3;

  glGenBuffers (1, &eglglessink->egl_context->position_buffer);
  glGenBuffers (1, &eglglessink->egl_context->index_buffer);
  if (got_gl_error ("glGenBuffers"))
    goto HANDLE_ERROR_LOCKED;

  glBindBuffer (GL_ARRAY_BUFFER, eglglessink->egl_context->position_buffer);
  if (got_gl_error ("glBindBuffer position_buffer"))
    goto HANDLE_ERROR_LOCKED;

  glBufferData (GL_ARRAY_BUFFER,
      sizeof (eglglessink->egl_context->position_array),
      eglglessink->egl_context->position_array, GL_STATIC_DRAW);
  if (got_gl_error ("glBufferData position_buffer"))
    goto HANDLE_ERROR_LOCKED;

  glBindBuffer (GL_ELEMENT_ARRAY_BUFFER,
      eglglessink->egl_context->index_buffer);
  if (got_gl_error ("glBindBuffer index_buffer"))
    goto HANDLE_ERROR_LOCKED;

  glBufferData (GL_ELEMENT_ARRAY_BUFFER,
      sizeof (eglglessink->egl_context->index_array),
      eglglessink->egl_context->index_array, GL_STATIC_DRAW);
  if (got_gl_error ("glBufferData index_buffer"))
    goto HANDLE_ERROR_LOCKED;

  eglglessink->egl_context->have_vbo = TRUE;
  GST_DEBUG_OBJECT (eglglessink, "VBO setup done");

  return TRUE;

HANDLE_ERROR_LOCKED:
  GST_ERROR_OBJECT (eglglessink, "Unable to perform VBO setup");
  return FALSE;
}

static void
gst_eglglessink_set_window_handle (GstXOverlay * overlay, guintptr id)
{
  GstEglGlesSink *eglglessink = GST_EGLGLESSINK (overlay);

  g_return_if_fail (GST_IS_EGLGLESSINK (eglglessink));
  GST_DEBUG_OBJECT (eglglessink, "We got a window handle: %p", (gpointer) id);

  /* OK, we have a new window */
  GST_OBJECT_LOCK (eglglessink);
  gst_egl_adaptation_set_window (eglglessink->egl_context, id);
  eglglessink->have_window = ((gpointer) id != NULL);
  GST_OBJECT_UNLOCK (eglglessink);

  return;
}

static void
gst_eglglessink_set_render_rectangle (GstXOverlay * overlay, gint x, gint y,
    gint width, gint height)
{
  GstEglGlesSink *eglglessink = GST_EGLGLESSINK (overlay);

  g_return_if_fail (GST_IS_EGLGLESSINK (eglglessink));

  GST_OBJECT_LOCK (eglglessink);
  eglglessink->render_region.x = x;
  eglglessink->render_region.y = y;
  eglglessink->render_region.w = width;
  eglglessink->render_region.h = height;
  eglglessink->render_region_changed = TRUE;
  eglglessink->render_region_user = (width != -1 && height != -1);
  GST_OBJECT_UNLOCK (eglglessink);

  return;
}

static void
queue_item_destroy (GstDataQueueItem * item)
{
  gst_mini_object_replace (&item->object, NULL);
  g_slice_free (GstDataQueueItem, item);
}

static GstFlowReturn
gst_eglglessink_queue_buffer (GstEglGlesSink * eglglessink, GstBuffer * buf)
{
  GstDataQueueItem *item;
  GstFlowReturn last_flow;

  g_mutex_lock (eglglessink->render_lock);
  last_flow = eglglessink->last_flow;
  g_mutex_unlock (eglglessink->render_lock);

  if (last_flow != GST_FLOW_OK)
    return last_flow;

  item = g_slice_new0 (GstDataQueueItem);

  item->object = GST_MINI_OBJECT_CAST (buf);
  item->size = (buf ? GST_BUFFER_SIZE (buf) : 0);
  item->duration = (buf ? GST_BUFFER_DURATION (buf) : GST_CLOCK_TIME_NONE);
  item->visible = (buf ? TRUE : FALSE);
  item->destroy = (GDestroyNotify) queue_item_destroy;

  GST_DEBUG_OBJECT (eglglessink, "Queueing buffer %" GST_PTR_FORMAT, buf);

  if (buf)
    g_mutex_lock (eglglessink->render_lock);
  if (!gst_data_queue_push (eglglessink->queue, item)) {
    item->destroy (item);
    g_mutex_unlock (eglglessink->render_lock);
    GST_DEBUG_OBJECT (eglglessink, "Flushing");
    return GST_FLOW_WRONG_STATE;
  }

  if (buf) {
    GST_DEBUG_OBJECT (eglglessink, "Waiting for buffer to be rendered");
    g_cond_wait (eglglessink->render_cond, eglglessink->render_lock);
    GST_DEBUG_OBJECT (eglglessink, "Buffer rendered: %s",
        gst_flow_get_name (eglglessink->last_flow));
    last_flow = eglglessink->last_flow;
    g_mutex_unlock (eglglessink->render_lock);
  }

  return (buf ? last_flow : GST_FLOW_OK);
}

static gboolean
gst_eglglessink_fill_texture (GstEglGlesSink * eglglessink, GstBuffer * buf)
{
  gint w, h;

  w = GST_VIDEO_SINK_WIDTH (eglglessink);
  h = GST_VIDEO_SINK_HEIGHT (eglglessink);

  GST_DEBUG_OBJECT (eglglessink,
      "Got good buffer %p. Sink geometry is %dx%d size %d", buf, w, h,
      buf ? GST_BUFFER_SIZE (buf) : -1);

  switch (eglglessink->format) {
    case GST_VIDEO_FORMAT_RGBA:
    case GST_VIDEO_FORMAT_BGRA:
    case GST_VIDEO_FORMAT_ARGB:
    case GST_VIDEO_FORMAT_ABGR:
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_BGRx:
    case GST_VIDEO_FORMAT_xRGB:
    case GST_VIDEO_FORMAT_xBGR:
      glActiveTexture (GL_TEXTURE0);
      glBindTexture (GL_TEXTURE_2D, eglglessink->egl_context->texture[0]);
      glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA,
          GL_UNSIGNED_BYTE, GST_BUFFER_DATA (buf));
      break;
    case GST_VIDEO_FORMAT_AYUV:
      glActiveTexture (GL_TEXTURE0);
      glBindTexture (GL_TEXTURE_2D, eglglessink->egl_context->texture[0]);
      glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA,
          GL_UNSIGNED_BYTE, GST_BUFFER_DATA (buf));
      break;
    case GST_VIDEO_FORMAT_Y444:
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
    case GST_VIDEO_FORMAT_Y42B:
    case GST_VIDEO_FORMAT_Y41B:{
      gint coffset, cw, ch;

      coffset =
          gst_video_format_get_component_offset (eglglessink->format, 0, w, h);
      cw = gst_video_format_get_component_width (eglglessink->format, 0, w);
      ch = gst_video_format_get_component_height (eglglessink->format, 0, h);
      glActiveTexture (GL_TEXTURE0);
      glBindTexture (GL_TEXTURE_2D, eglglessink->egl_context->texture[0]);
      glTexImage2D (GL_TEXTURE_2D, 0, GL_LUMINANCE, cw, ch, 0,
          GL_LUMINANCE, GL_UNSIGNED_BYTE, GST_BUFFER_DATA (buf) + coffset);
      coffset =
          gst_video_format_get_component_offset (eglglessink->format, 1, w, h);
      cw = gst_video_format_get_component_width (eglglessink->format, 1, w);
      ch = gst_video_format_get_component_height (eglglessink->format, 1, h);
      glActiveTexture (GL_TEXTURE1);
      glBindTexture (GL_TEXTURE_2D, eglglessink->egl_context->texture[1]);
      glTexImage2D (GL_TEXTURE_2D, 0, GL_LUMINANCE, cw, ch, 0,
          GL_LUMINANCE, GL_UNSIGNED_BYTE, GST_BUFFER_DATA (buf) + coffset);
      coffset =
          gst_video_format_get_component_offset (eglglessink->format, 2, w, h);
      cw = gst_video_format_get_component_width (eglglessink->format, 2, w);
      ch = gst_video_format_get_component_height (eglglessink->format, 2, h);
      glActiveTexture (GL_TEXTURE2);
      glBindTexture (GL_TEXTURE_2D, eglglessink->egl_context->texture[2]);
      glTexImage2D (GL_TEXTURE_2D, 0, GL_LUMINANCE, cw, ch, 0,
          GL_LUMINANCE, GL_UNSIGNED_BYTE, GST_BUFFER_DATA (buf) + coffset);
      break;
    }
    case GST_VIDEO_FORMAT_YUY2:
    case GST_VIDEO_FORMAT_YVYU:
    case GST_VIDEO_FORMAT_UYVY:
      glActiveTexture (GL_TEXTURE0);
      glBindTexture (GL_TEXTURE_2D, eglglessink->egl_context->texture[0]);
      glTexImage2D (GL_TEXTURE_2D, 0, GL_LUMINANCE_ALPHA, w, h, 0,
          GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE, GST_BUFFER_DATA (buf));
      glActiveTexture (GL_TEXTURE1);
      glBindTexture (GL_TEXTURE_2D, eglglessink->egl_context->texture[1]);
      glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA, GST_ROUND_UP_2 (w) / 2,
          h, 0, GL_RGBA, GL_UNSIGNED_BYTE, GST_BUFFER_DATA (buf));
      break;
    case GST_VIDEO_FORMAT_NV12:
    case GST_VIDEO_FORMAT_NV21:{
      gint coffset, cw, ch;

      coffset =
          gst_video_format_get_component_offset (eglglessink->format, 0, w, h);
      cw = gst_video_format_get_component_width (eglglessink->format, 0, w);
      ch = gst_video_format_get_component_height (eglglessink->format, 0, h);
      glActiveTexture (GL_TEXTURE0);
      glBindTexture (GL_TEXTURE_2D, eglglessink->egl_context->texture[0]);
      glTexImage2D (GL_TEXTURE_2D, 0, GL_LUMINANCE, cw, ch, 0,
          GL_LUMINANCE, GL_UNSIGNED_BYTE, GST_BUFFER_DATA (buf) + coffset);

      coffset =
          gst_video_format_get_component_offset (eglglessink->format,
          (eglglessink->format == GST_VIDEO_FORMAT_NV12 ? 1 : 2), w, h);
      cw = gst_video_format_get_component_width (eglglessink->format, 1, w);
      ch = gst_video_format_get_component_height (eglglessink->format, 1, h);
      glActiveTexture (GL_TEXTURE1);
      glBindTexture (GL_TEXTURE_2D, eglglessink->egl_context->texture[1]);
      glTexImage2D (GL_TEXTURE_2D, 0, GL_LUMINANCE_ALPHA, cw, ch, 0,
          GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE,
          GST_BUFFER_DATA (buf) + coffset);
      break;
    }
    default:
      g_assert_not_reached ();
      break;
  }

  if (got_gl_error ("glTexImage2D"))
    goto HANDLE_ERROR;

  return TRUE;

HANDLE_ERROR:
  return FALSE;
}

/* Rendering and display */
static GstFlowReturn
gst_eglglessink_upload (GstEglGlesSink * eglglessink, GstBuffer * buf)
{
  if (!buf) {
    GST_DEBUG_OBJECT (eglglessink, "Rendering previous buffer again");
  } else if (buf) {
    if (!gst_eglglessink_fill_texture (eglglessink, buf))
      goto HANDLE_ERROR;
  }

  return GST_FLOW_OK;

HANDLE_ERROR:
  {
    GST_ERROR_OBJECT (eglglessink, "Failed to upload texture");
    return GST_FLOW_ERROR;
  }
}

static GstFlowReturn
gst_eglglessink_render (GstEglGlesSink * eglglessink)
{
  guint dar_n, dar_d;
  gint i;
  gint w, h;

  w = GST_VIDEO_SINK_WIDTH (eglglessink);
  h = GST_VIDEO_SINK_HEIGHT (eglglessink);

  /* If no one has set a display rectangle on us initialize
   * a sane default. According to the docs on the xOverlay
   * interface we are supposed to fill the overlay 100%. We
   * do this trying to take PAR/DAR into account unless the
   * calling party explicitly ask us not to by setting
   * force_aspect_ratio to FALSE.
   */
  if (gst_egl_adaptation_update_surface_dimensions
      (eglglessink->egl_context) || eglglessink->render_region_changed
      || !eglglessink->display_region.w || !eglglessink->display_region.h
      || eglglessink->size_changed) {
    GST_OBJECT_LOCK (eglglessink);

    if (!eglglessink->render_region_user) {
      eglglessink->render_region.x = 0;
      eglglessink->render_region.y = 0;
      eglglessink->render_region.w = eglglessink->egl_context->surface_width;
      eglglessink->render_region.h = eglglessink->egl_context->surface_height;
    }
    eglglessink->render_region_changed = FALSE;
    eglglessink->size_changed = FALSE;

    if (!eglglessink->force_aspect_ratio) {
      eglglessink->display_region.x = 0;
      eglglessink->display_region.y = 0;
      eglglessink->display_region.w = eglglessink->render_region.w;
      eglglessink->display_region.h = eglglessink->render_region.h;
    } else {
      GstVideoRectangle frame;

      frame.x = 0;
      frame.y = 0;

      if (!gst_video_calculate_display_ratio (&dar_n, &dar_d,
              w, h,
              eglglessink->par_n,
              eglglessink->par_d,
              eglglessink->egl_context->pixel_aspect_ratio_n,
              eglglessink->egl_context->pixel_aspect_ratio_d)) {
        GST_WARNING_OBJECT (eglglessink, "Could not compute resulting DAR");
        frame.w = w;
        frame.h = h;
      } else {
        /* Find suitable matching new size acording to dar & par
         * rationale for prefering leaving the height untouched
         * comes from interlacing considerations.
         * XXX: Move this to gstutils?
         */
        if (h % dar_d == 0) {
          frame.w = gst_util_uint64_scale_int (h, dar_n, dar_d);
          frame.h = h;
        } else if (w % dar_n == 0) {
          frame.h = gst_util_uint64_scale_int (w, dar_d, dar_n);
          frame.w = w;
        } else {
          /* Neither width nor height can be precisely scaled.
           * Prefer to leave height untouched. See comment above.
           */
          frame.w = gst_util_uint64_scale_int (h, dar_n, dar_d);
          frame.h = h;
        }
      }

      gst_video_sink_center_rect (frame, eglglessink->render_region,
          &eglglessink->display_region, TRUE);
    }

    glViewport (eglglessink->render_region.x,
        eglglessink->egl_context->surface_height -
        eglglessink->render_region.y -
        eglglessink->render_region.h,
        eglglessink->render_region.w, eglglessink->render_region.h);

    /* Clear the surface once if its content is preserved */
    if (eglglessink->egl_context->buffer_preserved) {
      glClearColor (0.0, 0.0, 0.0, 1.0);
      glClear (GL_COLOR_BUFFER_BIT);
    }

    if (!gst_eglglessink_setup_vbo (eglglessink, FALSE)) {
      GST_OBJECT_UNLOCK (eglglessink);
      GST_ERROR_OBJECT (eglglessink, "VBO setup failed");
      goto HANDLE_ERROR;
    }
    GST_OBJECT_UNLOCK (eglglessink);
  }

  if (!eglglessink->egl_context->buffer_preserved) {
    /* Draw black borders */
    GST_DEBUG_OBJECT (eglglessink, "Drawing black border 1");
    glUseProgram (eglglessink->egl_context->glslprogram[1]);

    glVertexAttribPointer (eglglessink->egl_context->position_loc[1], 3,
        GL_FLOAT, GL_FALSE, sizeof (coord5), (gpointer) (4 * sizeof (coord5)));
    if (got_gl_error ("glVertexAttribPointer"))
      goto HANDLE_ERROR;

    glDrawElements (GL_TRIANGLE_STRIP, 4, GL_UNSIGNED_SHORT, 0);
    if (got_gl_error ("glDrawElements"))
      goto HANDLE_ERROR;

    GST_DEBUG_OBJECT (eglglessink, "Drawing black border 2");

    glVertexAttribPointer (eglglessink->egl_context->position_loc[1], 3,
        GL_FLOAT, GL_FALSE, sizeof (coord5), (gpointer) (8 * sizeof (coord5)));
    if (got_gl_error ("glVertexAttribPointer"))
      goto HANDLE_ERROR;

    glDrawElements (GL_TRIANGLE_STRIP, 4, GL_UNSIGNED_SHORT, 0);
    if (got_gl_error ("glDrawElements"))
      goto HANDLE_ERROR;
  }

  /* Draw video frame */
  GST_DEBUG_OBJECT (eglglessink, "Drawing video frame");
  glUseProgram (eglglessink->egl_context->glslprogram[0]);

  for (i = 0; i < eglglessink->egl_context->n_textures; i++) {
    glUniform1i (eglglessink->egl_context->tex_loc[0][i], i);
    if (got_gl_error ("glUniform1i"))
      goto HANDLE_ERROR;
  }

  glVertexAttribPointer (eglglessink->egl_context->position_loc[0], 3,
      GL_FLOAT, GL_FALSE, sizeof (coord5), (gpointer) (0 * sizeof (coord5)));
  if (got_gl_error ("glVertexAttribPointer"))
    goto HANDLE_ERROR;

  glVertexAttribPointer (eglglessink->egl_context->texpos_loc[0], 2, GL_FLOAT,
      GL_FALSE, sizeof (coord5), (gpointer) (3 * sizeof (gfloat)));
  if (got_gl_error ("glVertexAttribPointer"))
    goto HANDLE_ERROR;

  glDrawElements (GL_TRIANGLE_STRIP, 4, GL_UNSIGNED_SHORT, 0);
  if (got_gl_error ("glDrawElements"))
    goto HANDLE_ERROR;

  if (!gst_egl_adaptation_swap_buffers (eglglessink->egl_context)) {
    goto HANDLE_ERROR;
  }

  GST_DEBUG_OBJECT (eglglessink, "Succesfully rendered 1 frame");
  return GST_FLOW_OK;

HANDLE_ERROR:
  GST_ERROR_OBJECT (eglglessink, "Rendering disabled for this frame");

  return GST_FLOW_ERROR;
}

static GstFlowReturn
gst_eglglessink_show_frame (GstVideoSink * vsink, GstBuffer * buf)
{
  GstEglGlesSink *eglglessink;

  g_return_val_if_fail (buf != NULL, GST_FLOW_ERROR);

  eglglessink = GST_EGLGLESSINK (vsink);
  GST_DEBUG_OBJECT (eglglessink, "Got buffer: %p", buf);

  return gst_eglglessink_queue_buffer (eglglessink, gst_buffer_ref (buf));
}

static GstCaps *
gst_eglglessink_getcaps (GstBaseSink * bsink)
{
  GstEglGlesSink *eglglessink;
  GstCaps *ret = NULL;

  eglglessink = GST_EGLGLESSINK (bsink);

  GST_OBJECT_LOCK (eglglessink);
  if (eglglessink->sinkcaps) {
    ret = gst_caps_ref (eglglessink->sinkcaps);
  } else {
    ret =
        gst_caps_copy (gst_pad_get_pad_template_caps (GST_BASE_SINK_PAD
            (bsink)));
  }
  GST_OBJECT_UNLOCK (eglglessink);

  return ret;
}

static gboolean
gst_eglglessink_configure_caps (GstEglGlesSink * eglglessink, GstCaps * caps)
{
  gboolean ret = TRUE;
  gint width, height;
  int par_n, par_d;
  guintptr used_window = 0;

  if (!(ret = gst_video_format_parse_caps (caps, &eglglessink->format, &width,
              &height))) {
    GST_ERROR_OBJECT (eglglessink, "Got weird and/or incomplete caps");
    goto HANDLE_ERROR;
  }

  if (!(ret = gst_video_parse_caps_pixel_aspect_ratio (caps, &par_n, &par_d))) {
    par_n = 1;
    par_d = 1;
    GST_WARNING_OBJECT (eglglessink,
        "Can't parse PAR from caps. Using default: 1");
  }

  eglglessink->size_changed = (GST_VIDEO_SINK_WIDTH (eglglessink) != width ||
      GST_VIDEO_SINK_HEIGHT (eglglessink) != height ||
      eglglessink->par_n != par_n || eglglessink->par_d != par_d);

  eglglessink->par_n = par_n;
  eglglessink->par_d = par_d;
  GST_VIDEO_SINK_WIDTH (eglglessink) = width;
  GST_VIDEO_SINK_HEIGHT (eglglessink) = height;

  if (eglglessink->configured_caps) {
    GST_DEBUG_OBJECT (eglglessink, "Caps were already set");
    if (gst_caps_can_intersect (caps, eglglessink->configured_caps)) {
      GST_DEBUG_OBJECT (eglglessink, "Caps are compatible anyway");
      goto SUCCEED;
    }

    GST_DEBUG_OBJECT (eglglessink, "Caps are not compatible, reconfiguring");

    /* EGL/GLES cleanup */
    gst_egl_adaptation_cleanup (eglglessink->egl_context);

    gst_caps_unref (eglglessink->configured_caps);
    eglglessink->configured_caps = NULL;
  }

  if (!gst_egl_adaptation_choose_config (eglglessink->egl_context)) {
    GST_ERROR_OBJECT (eglglessink, "Couldn't choose EGL config");
    goto HANDLE_ERROR;
  }

  gst_caps_replace (&eglglessink->configured_caps, caps);

  /* By now the application should have set a window
   * if it meant to do so
   */
  GST_OBJECT_LOCK (eglglessink);
  if (!eglglessink->have_window) {

    GST_INFO_OBJECT (eglglessink,
        "No window. Will attempt internal window creation");
    if (!gst_eglglessink_create_window (eglglessink, width, height)) {
      GST_ERROR_OBJECT (eglglessink, "Internal window creation failed!");
      GST_OBJECT_UNLOCK (eglglessink);
      goto HANDLE_ERROR;
    }
    eglglessink->using_own_window = TRUE;
    gst_egl_adaptation_update_used_window (eglglessink->egl_context);
    eglglessink->have_window = TRUE;
  }
  used_window = gst_egl_adaptation_get_window (eglglessink->egl_context);
  GST_OBJECT_UNLOCK (eglglessink);
  gst_x_overlay_got_window_handle (GST_X_OVERLAY (eglglessink),
      (guintptr) used_window);

  if (!eglglessink->egl_context->have_surface) {
    if (!gst_egl_adaptation_init_egl_surface (eglglessink->egl_context,
            eglglessink->format)) {
      GST_ERROR_OBJECT (eglglessink, "Couldn't init EGL surface from window");
      goto HANDLE_ERROR;
    }
  }

SUCCEED:
  GST_INFO_OBJECT (eglglessink, "Configured caps successfully");
  return TRUE;

HANDLE_ERROR:
  GST_ERROR_OBJECT (eglglessink, "Configuring caps failed");
  return FALSE;
}

static gboolean
gst_eglglessink_setcaps (GstBaseSink * bsink, GstCaps * caps)
{
  GstEglGlesSink *eglglessink;

  eglglessink = GST_EGLGLESSINK (bsink);

  GST_DEBUG_OBJECT (eglglessink,
      "Current caps %" GST_PTR_FORMAT ", setting caps %"
      GST_PTR_FORMAT, eglglessink->current_caps, caps);

  gst_caps_replace (&eglglessink->current_caps, caps);

  return TRUE;
}

static gboolean
gst_eglglessink_open (GstEglGlesSink * eglglessink)
{
  if (!egl_init (eglglessink)) {
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_eglglessink_close (GstEglGlesSink * eglglessink)
{
  gst_egl_adaptation_terminate_display (eglglessink->egl_context);

  gst_caps_unref (eglglessink->sinkcaps);
  eglglessink->sinkcaps = NULL;
  eglglessink->egl_started = FALSE;

  return TRUE;
}

static GstStateChangeReturn
gst_eglglessink_change_state (GstElement * element, GstStateChange transition)
{
  GstEglGlesSink *eglglessink;
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  eglglessink = GST_EGLGLESSINK (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (!gst_eglglessink_open (eglglessink)) {
        ret = GST_STATE_CHANGE_FAILURE;
        goto done;
      }
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      if (!gst_eglglessink_start (eglglessink)) {
        ret = GST_STATE_CHANGE_FAILURE;
        goto done;
      }
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      if (!gst_eglglessink_close (eglglessink)) {
        ret = GST_STATE_CHANGE_FAILURE;
        goto done;
      }
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      if (!gst_eglglessink_stop (eglglessink)) {
        ret = GST_STATE_CHANGE_FAILURE;
        goto done;
      }
      break;
    default:
      break;
  }

done:
  return ret;
}

static void
gst_eglglessink_finalize (GObject * object)
{
  GstEglGlesSink *eglglessink;

  g_return_if_fail (GST_IS_EGLGLESSINK (object));

  eglglessink = GST_EGLGLESSINK (object);

  if (eglglessink->queue)
    g_object_unref (eglglessink->queue);
  eglglessink->queue = NULL;

  g_cond_free (eglglessink->render_cond);
  g_mutex_free (eglglessink->render_lock);

  gst_egl_adaptation_free (eglglessink->egl_context);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_eglglessink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstEglGlesSink *eglglessink;

  g_return_if_fail (GST_IS_EGLGLESSINK (object));

  eglglessink = GST_EGLGLESSINK (object);

  switch (prop_id) {
    case PROP_CREATE_WINDOW:
      eglglessink->create_window = g_value_get_boolean (value);
      break;
    case PROP_FORCE_ASPECT_RATIO:
      eglglessink->force_aspect_ratio = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_eglglessink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstEglGlesSink *eglglessink;

  g_return_if_fail (GST_IS_EGLGLESSINK (object));

  eglglessink = GST_EGLGLESSINK (object);

  switch (prop_id) {
    case PROP_CREATE_WINDOW:
      g_value_set_boolean (value, eglglessink->create_window);
      break;
    case PROP_FORCE_ASPECT_RATIO:
      g_value_set_boolean (value, eglglessink->force_aspect_ratio);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_eglglessink_base_init (gpointer gclass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (gclass);

  gst_element_class_set_details_simple (element_class,
      "EGL/GLES vout Sink",
      "Sink/Video",
      "An EGL/GLES Video Output Sink Implementing the XOverlay interface",
      "Reynaldo H. Verdejo Pinochet <reynaldo@collabora.com>, "
      "Sebastian Dröge <sebastian.droege@collabora.co.uk>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_eglglessink_sink_template_factory));
}

/* initialize the eglglessink's class */
static void
gst_eglglessink_class_init (GstEglGlesSinkClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSinkClass *gstbasesink_class;
  GstVideoSinkClass *gstvideosink_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasesink_class = (GstBaseSinkClass *) klass;
  gstvideosink_class = (GstVideoSinkClass *) klass;

  gobject_class->set_property = gst_eglglessink_set_property;
  gobject_class->get_property = gst_eglglessink_get_property;
  gobject_class->finalize = gst_eglglessink_finalize;

  gstelement_class->change_state = gst_eglglessink_change_state;

  gstbasesink_class->set_caps = GST_DEBUG_FUNCPTR (gst_eglglessink_setcaps);
  gstbasesink_class->get_caps = GST_DEBUG_FUNCPTR (gst_eglglessink_getcaps);

  gstvideosink_class->show_frame =
      GST_DEBUG_FUNCPTR (gst_eglglessink_show_frame);

  g_object_class_install_property (gobject_class, PROP_CREATE_WINDOW,
      g_param_spec_boolean ("create-window", "Create Window",
          "If set to true, the sink will attempt to create it's own window to "
          "render to if none is provided. This is currently only supported "
          "when the sink is used under X11",
          TRUE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_FORCE_ASPECT_RATIO,
      g_param_spec_boolean ("force-aspect-ratio",
          "Respect aspect ratio when scaling",
          "If set to true, the sink will attempt to preserve the incoming "
          "frame's geometry while scaling, taking both the storage's and "
          "display's pixel aspect ratio into account",
          TRUE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static gboolean
queue_check_full_func (GstDataQueue * queue, guint visible, guint bytes,
    guint64 time, gpointer checkdata)
{
  return visible != 0;
}

static void
gst_eglglessink_init (GstEglGlesSink * eglglessink,
    GstEglGlesSinkClass * gclass)
{
  /* Init defaults */

  /** Flags */
  eglglessink->have_window = FALSE;
  eglglessink->egl_started = FALSE;
  eglglessink->using_own_window = FALSE;

  /** Props */
  eglglessink->create_window = TRUE;
  eglglessink->force_aspect_ratio = TRUE;

  eglglessink->render_region.x = 0;
  eglglessink->render_region.y = 0;
  eglglessink->render_region.w = -1;
  eglglessink->render_region.h = -1;
  eglglessink->render_region_changed = TRUE;
  eglglessink->render_region_user = FALSE;

  eglglessink->par_n = 1;
  eglglessink->par_d = 1;

  eglglessink->render_lock = g_mutex_new ();
  eglglessink->render_cond = g_cond_new ();
  eglglessink->queue = gst_data_queue_new (queue_check_full_func, NULL);
  eglglessink->last_flow = GST_FLOW_WRONG_STATE;

  eglglessink->egl_context =
      gst_egl_adaptation_new (GST_ELEMENT_CAST (eglglessink));
}

/* Interface initializations. Used here for initializing the XOverlay
 * Interface.
 */
static void
gst_eglglessink_init_interfaces (GType type)
{
  static const GInterfaceInfo implements_info = {
    (GInterfaceInitFunc) gst_eglglessink_implements_init, NULL, NULL
  };

  static const GInterfaceInfo xoverlay_info = {
    (GInterfaceInitFunc) gst_eglglessink_xoverlay_init, NULL, NULL
  };

  g_type_add_interface_static (type, GST_TYPE_IMPLEMENTS_INTERFACE,
      &implements_info);
  g_type_add_interface_static (type, GST_TYPE_X_OVERLAY, &xoverlay_info);
}

/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */
static gboolean
eglglessink_plugin_init (GstPlugin * plugin)
{
  /* debug category for fltering log messages */
  GST_DEBUG_CATEGORY_INIT (gst_eglglessink_debug, "eglglessink",
      0, "Simple EGL/GLES Sink");

#ifdef USE_EGL_RPI
  GST_DEBUG ("Initialize BCM host");
  bcm_host_init ();
#endif

  return gst_element_register (plugin, "eglglessink", GST_RANK_PRIMARY,
      GST_TYPE_EGLGLESSINK);
}

/* gstreamer looks for this structure to register eglglessinks */
#ifdef GST_PLUGIN_DEFINE2
GST_PLUGIN_DEFINE2 (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    eglglessink,
    "EGL/GLES sink",
    eglglessink_plugin_init,
    VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
#else
GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "eglglessink",
    "EGL/GLES sink",
    eglglessink_plugin_init,
    VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
#endif
