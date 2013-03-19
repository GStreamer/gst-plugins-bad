/*
 * GStreamer EGL/GLES Sink Adaptation
 * Copyright (C) 2013 Collabora Ltd.
 *   @author: Thiago Santos <thiago.sousa.santos@collabora.com>
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

#include "gstegladaptation.h"

#include "video_platform_wrapper.h"

/*
 * GstEglGlesRenderContext:
 * @config: Current EGL config
 * @eglcontext: Current EGL context
 * @display: Current EGL display connection
 * @window: Current EGL window asociated with the display connection
 * @used_window: Last seen EGL window asociated with the display connection
 * @surface: EGL surface the sink is rendering into
 * @fragshader: Fragment shader
 * @vertshader: Vertex shader
 * @glslprogram: Compiled and linked GLSL program in use for rendering
 * @texture Texture units in use
 * @pixel_aspect_ratio: EGL display aspect ratio
 * @egl_minor: EGL version (minor)
 * @egl_major: EGL version (major)
 * @n_textures: Texture units count
 * @position_loc: Index of the position vertex attribute array
 * @texpos_loc: Index of the textpos vertex attribute array
 * @position_array: VBO position array
 * @texpos_array: VBO texpos array
 * @index_array: VBO index array
 * @position_buffer: Position buffer object name
 * @texpos_buffer: Texpos buffer object name
 * @index_buffer: Index buffer object name
 *
 * This struct holds the sink's EGL/GLES rendering context.
 */
struct _GstEglGlesRenderContext
{
  EGLConfig config;
  EGLContext eglcontext;
  EGLSurface surface;
  EGLint egl_minor, egl_major;

  EGLNativeWindowType window, used_window;
  EGLDisplay display;

};

/* Some EGL implementations are reporting wrong
 * values for the display's EGL_PIXEL_ASPECT_RATIO.
 * They are required by the khronos specs to report
 * this value as w/h * EGL_DISPLAY_SCALING (Which is
 * a constant with value 10000) but at least the
 * Galaxy SIII (Android) is reporting just 1 when
 * w = h. We use these two to bound returned values to
 * sanity.
 */
#define EGL_SANE_DAR_MIN ((EGL_DISPLAY_SCALING)/10)
#define EGL_SANE_DAR_MAX ((EGL_DISPLAY_SCALING)*10)

gboolean
gst_egl_adaptation_init_display (GstEglAdaptationContext * ctx)
{
  EGLDisplay display;
  GST_DEBUG_OBJECT (ctx->element, "Enter EGL initial configuration");

#ifdef USE_EGL_RPI
  /* See https://github.com/raspberrypi/firmware/issues/99 */
  if (!eglMakeCurrent ((EGLDisplay) 1, EGL_NO_SURFACE, EGL_NO_SURFACE,
          EGL_NO_CONTEXT)) {
    got_egl_error ("eglMakeCurrent");
    GST_ERROR_OBJECT (ctx->element, "Couldn't unbind context");
    return FALSE;
  }
#endif

  display = eglGetDisplay (EGL_DEFAULT_DISPLAY);
  if (display == EGL_NO_DISPLAY) {
    GST_ERROR_OBJECT (ctx->element, "Could not get EGL display connection");
    goto HANDLE_ERROR;          /* No EGL error is set by eglGetDisplay() */
  }
  ctx->eglglesctx->display = display;

  if (!eglInitialize (display,
          &ctx->eglglesctx->egl_major, &ctx->eglglesctx->egl_minor)) {
    got_egl_error ("eglInitialize");
    GST_ERROR_OBJECT (ctx->element, "Could not init EGL display connection");
    goto HANDLE_EGL_ERROR;
  }

  /* Check against required EGL version
   * XXX: Need to review the version requirement in terms of the needed API
   */
  if (ctx->eglglesctx->egl_major < GST_EGLGLESSINK_EGL_MIN_VERSION) {
    GST_ERROR_OBJECT (ctx->element, "EGL v%d needed, but you only have v%d.%d",
        GST_EGLGLESSINK_EGL_MIN_VERSION, ctx->eglglesctx->egl_major,
        ctx->eglglesctx->egl_minor);
    goto HANDLE_ERROR;
  }

  GST_INFO_OBJECT (ctx->element, "System reports supported EGL version v%d.%d",
      ctx->eglglesctx->egl_major, ctx->eglglesctx->egl_minor);

  eglBindAPI (EGL_OPENGL_ES_API);

  return TRUE;

  /* Errors */
HANDLE_EGL_ERROR:
  GST_ERROR_OBJECT (ctx->element, "EGL call returned error %x", eglGetError ());
HANDLE_ERROR:
  GST_ERROR_OBJECT (ctx->element, "Couldn't setup window/surface from handle");
  return FALSE;
}

void
gst_egl_adaptation_context_terminate_display (GstEglAdaptationContext * ctx)
{
  if (ctx->eglglesctx->display) {
    eglTerminate (ctx->eglglesctx->display);
    ctx->eglglesctx->display = NULL;
  }
}


gboolean
_gst_egl_choose_config (GstEglAdaptationContext * ctx, gboolean try_only,
    gint * num_configs)
{
  EGLint cfg_number;
  gboolean ret;
  EGLConfig *config = NULL;

  if (!try_only)
    config = &ctx->eglglesctx->config;

  ret = eglChooseConfig (ctx->eglglesctx->display,
      eglglessink_RGBA8888_attribs, config, 1, &cfg_number) != EGL_FALSE;

  if (num_configs)
    *num_configs = cfg_number;
  return ret;
}

gboolean
gst_egl_adaptation_create_egl_context (GstEglAdaptationContext * ctx)
{
  EGLint con_attribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };

  ctx->eglglesctx->eglcontext =
      eglCreateContext (ctx->eglglesctx->display,
      ctx->eglglesctx->config, EGL_NO_CONTEXT, con_attribs);

  if (ctx->eglglesctx->eglcontext == EGL_NO_CONTEXT) {
    return FALSE;
  }

  GST_DEBUG_OBJECT (ctx->element, "EGL Context: %p",
      ctx->eglglesctx->eglcontext);

  return TRUE;
}

gboolean
gst_egl_adaptation_context_make_current (GstEglAdaptationContext * ctx,
    gboolean bind)
{
  g_assert (ctx->eglglesctx->display != NULL);

  if (bind && ctx->eglglesctx->surface && ctx->eglglesctx->eglcontext) {
    EGLContext *cur_ctx = eglGetCurrentContext ();

    if (cur_ctx == ctx->eglglesctx->eglcontext) {
      GST_DEBUG_OBJECT (ctx->element,
          "Already attached the context to thread %p", g_thread_self ());
      return TRUE;
    }

    GST_DEBUG_OBJECT (ctx->element, "Attaching context to thread %p",
        g_thread_self ());
    if (!eglMakeCurrent (ctx->eglglesctx->display,
            ctx->eglglesctx->surface, ctx->eglglesctx->surface,
            ctx->eglglesctx->eglcontext)) {
      got_egl_error ("eglMakeCurrent");
      GST_ERROR_OBJECT (ctx->element, "Couldn't bind context");
      return FALSE;
    }
  } else {
    GST_DEBUG_OBJECT (ctx->element, "Detaching context from thread %p",
        g_thread_self ());
    if (!eglMakeCurrent (ctx->eglglesctx->display, EGL_NO_SURFACE,
            EGL_NO_SURFACE, EGL_NO_CONTEXT)) {
      got_egl_error ("eglMakeCurrent");
      GST_ERROR_OBJECT (ctx->element, "Couldn't unbind context");
      return FALSE;
    }
  }

  return TRUE;
}

gboolean
gst_egl_adaptation_create_surface (GstEglAdaptationContext * ctx)
{
  ctx->eglglesctx->surface =
      eglCreateWindowSurface (ctx->eglglesctx->display,
      ctx->eglglesctx->config, ctx->eglglesctx->used_window, NULL);

  if (ctx->eglglesctx->surface == EGL_NO_SURFACE) {
    got_egl_error ("eglCreateWindowSurface");
    GST_ERROR_OBJECT (ctx->element, "Can't create surface");
    return FALSE;
  }
  return TRUE;
}

void
gst_egl_adaptation_query_buffer_preserved (GstEglAdaptationContext * ctx)
{
  EGLint swap_behavior;

  ctx->buffer_preserved = FALSE;
  if (eglQuerySurface (ctx->eglglesctx->display,
          ctx->eglglesctx->surface, EGL_SWAP_BEHAVIOR, &swap_behavior)) {
    GST_DEBUG_OBJECT (ctx->element, "Buffer swap behavior %x", swap_behavior);
    ctx->buffer_preserved = swap_behavior == EGL_BUFFER_PRESERVED;
  } else {
    GST_DEBUG_OBJECT (ctx->element, "Can't query buffer swap behavior");
  }
}

void
gst_egl_adaptation_query_par (GstEglAdaptationContext * ctx)
{
  EGLint display_par;

  /* Save display's pixel aspect ratio
   *
   * DAR is reported as w/h * EGL_DISPLAY_SCALING wich is
   * a constant with value 10000. This attribute is only
   * supported if the EGL version is >= 1.2
   * XXX: Setup this as a property.
   * or some other one time check. Right now it's being called once
   * per frame.
   */
  if (ctx->eglglesctx->egl_major == 1 && ctx->eglglesctx->egl_minor < 2) {
    GST_DEBUG_OBJECT (ctx->element, "Can't query PAR. Using default: %dx%d",
        EGL_DISPLAY_SCALING, EGL_DISPLAY_SCALING);
    ctx->pixel_aspect_ratio = EGL_DISPLAY_SCALING;
  } else {
    eglQuerySurface (ctx->eglglesctx->display,
        ctx->eglglesctx->surface, EGL_PIXEL_ASPECT_RATIO, &display_par);
    /* Fix for outbound DAR reporting on some implementations not
     * honoring the 'should return w/h * EGL_DISPLAY_SCALING' spec
     * requirement
     */
    if (display_par == EGL_UNKNOWN || display_par < EGL_SANE_DAR_MIN ||
        display_par > EGL_SANE_DAR_MAX) {
      GST_DEBUG_OBJECT (ctx->element, "Nonsensical PAR value returned: %d. "
          "Bad EGL implementation? "
          "Will use default: %d/%d", ctx->pixel_aspect_ratio,
          EGL_DISPLAY_SCALING, EGL_DISPLAY_SCALING);
      ctx->pixel_aspect_ratio = EGL_DISPLAY_SCALING;
    } else {
      ctx->pixel_aspect_ratio = display_par;
    }
  }
}

/* XXX: Lock eglgles context? */
/* TODO refactor only to get the w/h and let common code do the updates */
gboolean
gst_egl_adaptation_context_update_surface_dimensions (GstEglAdaptationContext *
    ctx)
{
  gint width, height;

  /* Save surface dims */
  eglQuerySurface (ctx->eglglesctx->display, ctx->eglglesctx->surface,
      EGL_WIDTH, &width);
  eglQuerySurface (ctx->eglglesctx->display, ctx->eglglesctx->surface,
      EGL_HEIGHT, &height);

  if (width != ctx->surface_width || height != ctx->surface_height) {
    ctx->surface_width = width;
    ctx->surface_height = height;
    GST_INFO_OBJECT (ctx->element, "Got surface of %dx%d pixels", width,
        height);
    return TRUE;
  }

  return FALSE;
}

/* Prints avilable EGL/GLES extensions 
 * If another rendering path is implemented this is the place
 * where you want to check for the availability of its supporting
 * EGL/GLES extensions.
 */
void
gst_egl_adaptation_context_init_egl_exts (GstEglAdaptationContext * ctx)
{
  const char *eglexts;
  unsigned const char *glexts;

  eglexts = eglQueryString (ctx->eglglesctx->display, EGL_EXTENSIONS);
  glexts = glGetString (GL_EXTENSIONS);

  GST_DEBUG_OBJECT (ctx->element, "Available EGL extensions: %s\n",
      GST_STR_NULL (eglexts));
  GST_DEBUG_OBJECT (ctx->element, "Available GLES extensions: %s\n",
      GST_STR_NULL ((const char *) glexts));

  return;
}

void
gst_egl_adaptation_destroy_surface (GstEglAdaptationContext * ctx)
{
  if (ctx->eglglesctx->surface) {
    eglDestroySurface (ctx->eglglesctx->display, ctx->eglglesctx->surface);
    ctx->eglglesctx->surface = NULL;
    ctx->have_surface = FALSE;
  }
}

void
gst_egl_adaptation_destroy_context (GstEglAdaptationContext * ctx)
{
  if (ctx->eglglesctx->eglcontext) {
    eglDestroyContext (ctx->eglglesctx->display, ctx->eglglesctx->eglcontext);
    ctx->eglglesctx->eglcontext = NULL;
  }
}

void
gst_egl_adaptation_context_bind_API (GstEglAdaptationContext * ctx)
{
  eglBindAPI (EGL_OPENGL_ES_API);
}

gboolean
gst_egl_adaptation_context_swap_buffers (GstEglAdaptationContext * ctx)
{
  gboolean ret =
      eglSwapBuffers (ctx->eglglesctx->display, ctx->eglglesctx->surface);
  if (ret == EGL_FALSE) {
    got_egl_error ("eglSwapBuffers");
  }
  return ret;
}

gboolean
gst_egl_adaptation_create_native_window (GstEglAdaptationContext * ctx,
    gint width, gint height, gpointer * own_window_data)
{
  EGLNativeWindowType window =
      platform_create_native_window (width, height, own_window_data);
  if (window)
    gst_egl_adaptation_context_set_window (ctx, window);
  GST_DEBUG_OBJECT (ctx->element, "Using window handle %p", window);
  return window != 0;
}

void
gst_egl_adaptation_destroy_native_window (GstEglAdaptationContext * ctx,
    gpointer * own_window_data)
{
  platform_destroy_native_window (ctx->eglglesctx->display,
      ctx->eglglesctx->used_window, own_window_data);
  ctx->eglglesctx->used_window = 0;
}

void
gst_egl_adaptation_context_init (GstEglAdaptationContext * ctx)
{
  ctx->eglglesctx = g_new0 (GstEglGlesRenderContext, 1);
}

void
gst_egl_adaptation_context_deinit (GstEglAdaptationContext * ctx)
{
  g_free (ctx->eglglesctx);
}

void
gst_egl_adaptation_context_set_window (GstEglAdaptationContext * ctx,
    guintptr window)
{
  ctx->eglglesctx->window = (EGLNativeWindowType) window;
}

void
gst_egl_adaptation_context_update_used_window (GstEglAdaptationContext * ctx)
{
  ctx->eglglesctx->used_window = ctx->eglglesctx->window;
}

guintptr
gst_egl_adaptation_context_get_window (GstEglAdaptationContext * ctx)
{
  return ctx->eglglesctx->window;
}
