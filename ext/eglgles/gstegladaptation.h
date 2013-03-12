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

#include <gst/gst.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#ifdef USE_EGL_RPI
#include <bcm_host.h>
#endif

#include "video_platform_wrapper.h"

#define GST_EGLGLESSINK_IMAGE_NOFMT 0
#define GST_EGLGLESSINK_IMAGE_RGB888 1
#define GST_EGLGLESSINK_IMAGE_RGB565 2
#define GST_EGLGLESSINK_IMAGE_RGBA8888 3
#define GST_EGLGLESSINK_EGL_MIN_VERSION 1
G_BEGIN_DECLS

typedef struct GstEglAdaptationContext GstEglAdaptationContext;
typedef struct _GstEglGlesRenderContext GstEglGlesRenderContext;
typedef struct _GstEglGlesImageFmt GstEglGlesImageFmt;

typedef struct _coord5
{
  float x;
  float y;
  float z;
  float a;                      /* texpos x */
  float b;                      /* texpos y */
} coord5;

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
 * @surface_width: Pixel width of the surface the sink is rendering into
 * @surface_height: Pixel height of the surface the sink is rendering into
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
  EGLDisplay display;
  EGLNativeWindowType window, used_window;
  EGLSurface surface;
  gboolean buffer_preserved;
  GLuint fragshader[3]; /* frame, border, frame-platform */
  GLuint vertshader[3]; /* frame, border, frame-platform */
  GLuint glslprogram[3]; /* frame, border, frame-platform */
  GLuint texture[3]; /* RGB/Y, U/UV, V */
  EGLint surface_width;
  EGLint surface_height;
  EGLint pixel_aspect_ratio;
  EGLint egl_minor, egl_major;
  gint n_textures;

  /* shader vars */
  GLuint position_loc[3]; /* frame, border, frame-platform */
  GLuint texpos_loc[2]; /* frame, frame-platform */
  GLuint tex_loc[2][3]; /* [frame, frame-platform] RGB/Y, U/UV, V */
  coord5 position_array[12];    /* 4 x Frame, 4 x Border1, 4 x Border2 */
  unsigned short index_array[4];
  unsigned int position_buffer, index_buffer;
};

/*
 * GstEglGlesImageFmt:
 * @fmt: Internal identifier for the EGL attribs / GST caps pairing
 * @attribs: Pointer to the set of EGL attributes asociated with this format
 * @caps: Pointer to the GST caps asociated with this format
 *
 * This struct holds a pairing between GST caps and the matching EGL attributes
 * associated with a given pixel format
 */
struct _GstEglGlesImageFmt
{
  gint fmt;                     /* Private identifier */
  const EGLint *attribs;        /* EGL Attributes */
  GstCaps *caps;                /* Matching caps for the attribs */
};


/*
 * GstEglAdaptationContext:
 * @have_vbo: Set if the GLES VBO setup has been performed
 * @have_texture: Set if the GLES texture setup has been performed
 * @have_surface: Set if the EGL surface setup has been performed
 *  
 * The #GstEglAdaptationContext data structure.
 */
struct GstEglAdaptationContext
{
  GstElement *element;
  GstEglGlesRenderContext eglglesctx;

  gboolean have_vbo;
  gboolean have_texture;
  gboolean have_surface;;
};

GstEglAdaptationContext * gst_egl_adaptation_context_new (GstElement * element);
void gst_egl_adaptation_context_free (GstEglAdaptationContext * ctx);

gboolean gst_egl_adaptation_init_display (GstEglAdaptationContext * ctx);
gboolean gst_egl_adaptation_choose_config (GstEglAdaptationContext * ctx);
gint gst_egl_adaptation_context_fill_supported_fbuffer_configs (GstEglAdaptationContext * ctx, GstCaps ** ret_caps);
gboolean gst_egl_adaptation_context_make_current (GstEglAdaptationContext * ctx, gboolean bind);
void gst_egl_adaptation_context_cleanup (GstEglAdaptationContext * ctx);
gboolean gst_egl_adaptation_init_egl_surface (GstEglAdaptationContext * ctx, GstVideoFormat format);
gboolean gst_egl_adaptation_context_update_surface_dimensions (GstEglAdaptationContext * ctx);
void gst_egl_adaptation_context_terminate_display(GstEglAdaptationContext * ctx);
void gst_egl_adaptation_context_bind_API (GstEglAdaptationContext * ctx);
gboolean gst_egl_adaptation_context_swap_buffers (GstEglAdaptationContext * ctx);
void gst_egl_adaptation_context_init_egl_exts (GstEglAdaptationContext * ctx);

gboolean got_egl_error (const char *wtf); 
gboolean got_gl_error (const char *wtf);

G_END_DECLS

#endif /* __GST_EGL_ADAPTATION_H__ */
