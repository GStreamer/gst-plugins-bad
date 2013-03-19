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

#ifndef __GST_EGL_ADAPTATION_H__
#define __GST_EGL_ADAPTATION_H__

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gst/gst.h>

#ifndef HAVE_IOS
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#endif

#ifdef USE_EGL_RPI
#include <bcm_host.h>
#endif

#include <gst/video/video.h>

#define GST_EGLGLESSINK_IMAGE_NOFMT 0
#define GST_EGLGLESSINK_IMAGE_RGB888 1
#define GST_EGLGLESSINK_IMAGE_RGB565 2
#define GST_EGLGLESSINK_IMAGE_RGBA8888 3
#define GST_EGLGLESSINK_EGL_MIN_VERSION 1
G_BEGIN_DECLS

/* will probably move elsewhere */
static const EGLint eglglessink_RGBA8888_attribs[] = {
  EGL_RED_SIZE, 8,
  EGL_GREEN_SIZE, 8,
  EGL_BLUE_SIZE, 8,
  EGL_ALPHA_SIZE, 8,
  EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
  EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
  EGL_NONE
};

typedef struct GstEglAdaptationContext GstEglAdaptationContext;
typedef struct _GstEglGlesRenderContext GstEglGlesRenderContext;
typedef struct _GstEglGlesImageFmt GstEglGlesImageFmt;
typedef struct _GstEaglContext GstEaglContext;

typedef struct _coord5
{
  float x;
  float y;
  float z;
  float a;                      /* texpos x */
  float b;                      /* texpos y */
} coord5;

/*
 * GstEglAdaptationContext:
 * @have_vbo: Set if the GLES VBO setup has been performed
 * @have_texture: Set if the GLES texture setup has been performed
 * @have_surface: Set if the EGL surface setup has been performed
 * @surface_width: Pixel width of the surface the sink is rendering into
 * @surface_height: Pixel height of the surface the sink is rendering into
 *  
 * The #GstEglAdaptationContext data structure.
 */
struct GstEglAdaptationContext
{
  GstElement *element;

  unsigned int position_buffer, index_buffer;

  gboolean have_vbo;
  gboolean have_texture;
  gboolean have_surface;;

  gboolean buffer_preserved;

  gint surface_width;
  gint surface_height;
  gint pixel_aspect_ratio;

  GLuint fragshader[3]; /* frame, border, frame-platform */
  GLuint vertshader[3]; /* frame, border, frame-platform */
  GLuint glslprogram[3]; /* frame, border, frame-platform */
  GLuint texture[3]; /* RGB/Y, U/UV, V */
  gint n_textures;

  /* shader vars */
  GLuint position_loc[3]; /* frame, border, frame-platform */
  GLuint texpos_loc[2]; /* frame, frame-platform */
  GLuint tex_loc[2][3]; /* [frame, frame-platform] RGB/Y, U/UV, V */
  coord5 position_array[12];    /* 4 x Frame, 4 x Border1, 4 x Border2 */
  unsigned short index_array[4];

#if HAVE_IOS
  GstEaglContext *eaglctx;
#else
  GstEglGlesRenderContext *eglglesctx;
#endif
};

GstEglAdaptationContext * gst_egl_adaptation_context_new (GstElement * element);
void gst_egl_adaptation_context_init (GstEglAdaptationContext * ctx);
void gst_egl_adaptation_context_deinit (GstEglAdaptationContext * ctx);
void gst_egl_adaptation_context_free (GstEglAdaptationContext * ctx);

/* platform window */
gboolean gst_egl_adaptation_create_native_window (GstEglAdaptationContext * ctx, gint width, gint height, gpointer * own_window_data);
void gst_egl_adaptation_destroy_native_window (GstEglAdaptationContext * ctx, gpointer * own_window_data);

/* Initialization */
gboolean gst_egl_adaptation_init_display (GstEglAdaptationContext * ctx);
gint gst_egl_adaptation_context_fill_supported_fbuffer_configs (GstEglAdaptationContext * ctx, GstCaps ** ret_caps);
gboolean gst_egl_adaptation_init_egl_surface (GstEglAdaptationContext * ctx, GstVideoFormat format);
void gst_egl_adaptation_context_init_egl_exts (GstEglAdaptationContext * ctx);

/* cleanup */
void gst_egl_adaptation_context_cleanup (GstEglAdaptationContext * ctx);
void gst_egl_adaptation_context_terminate_display(GstEglAdaptationContext * ctx);

/* configuration */
gboolean gst_egl_adaptation_choose_config (GstEglAdaptationContext * ctx);
gboolean gst_egl_adaptation_context_make_current (GstEglAdaptationContext * ctx, gboolean bind);
gboolean gst_egl_adaptation_context_update_surface_dimensions (GstEglAdaptationContext * ctx);

/* rendering */
void gst_egl_adaptation_context_bind_API (GstEglAdaptationContext * ctx);
gboolean gst_egl_adaptation_context_swap_buffers (GstEglAdaptationContext * ctx);

/* get/set */
void gst_egl_adaptation_context_set_window (GstEglAdaptationContext * ctx, guintptr window);
void gst_egl_adaptation_context_update_used_window (GstEglAdaptationContext * ctx);
guintptr gst_egl_adaptation_context_get_window (GstEglAdaptationContext * ctx);
GLuint gst_egl_adaptation_context_get_texture (GstEglAdaptationContext * ctx, gint i);
gint gst_egl_adaptation_context_get_surface_width (GstEglAdaptationContext * ctx);
gint gst_egl_adaptation_context_get_surface_height (GstEglAdaptationContext * ctx);

/* error handling */
gboolean got_egl_error (const char *wtf); 
gboolean got_gl_error (const char *wtf);

/* platform specific helpers */
gboolean _gst_egl_choose_config (GstEglAdaptationContext * ctx, gboolean try_only, gint * num_configs);
gboolean gst_egl_adaptation_create_egl_context (GstEglAdaptationContext * ctx);
gboolean gst_egl_adaptation_create_surface (GstEglAdaptationContext * ctx);
void gst_egl_adaptation_query_buffer_preserved (GstEglAdaptationContext * ctx);
void gst_egl_adaptation_query_par (GstEglAdaptationContext * ctx);
void gst_egl_adaptation_destroy_surface (GstEglAdaptationContext * ctx);
void gst_egl_adaptation_destroy_context (GstEglAdaptationContext * ctx);

G_END_DECLS

#endif /* __GST_EGL_ADAPTATION_H__ */
