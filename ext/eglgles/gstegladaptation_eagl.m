/*
 * GStreamer EGL/GLES Sink Adaptation for IOS
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

typedef struct _GstEaglContext
{
  EAGLContext *eagl_context;
  GLUint framebuffer;
  GLUint color_renderbuffer;
} GstEaglContext;

void
gst_egl_adaptation_context_init (GstEglAdaptationContext * ctx)
{
  ctx->eaglctx = g_new0 (GstEaglContext, 1);
}

void
gst_egl_adaptation_context_deinit (GstEglAdaptationContext * ctx)
{
  g_free (ctx->eaglctx);
}

gboolean
gst_egl_adaptation_init_display (GstEglAdaptationContext * ctx)
{
  /* NOP - the display should be initialized by the application */
}

void
gst_egl_adaptation_context_terminate_display (GstEglAdaptationContext * ctx)
{
  /* NOP */
}

void
gst_egl_adaptation_context_bind_API (GstEglAdaptationContext * ctx)
{
  /* NOP */
}

gboolean
gst_egl_adaptation_create_native_window (GstEglAdaptationContext * ctx, gint width, gint height, gpointer * own_window_data)
{
}

void
gst_egl_adaptation_destroy_native_window (GstEglAdaptationContext * ctx, gpointer * own_window_data)
{
}

gboolean
gst_egl_adaptation_create_egl_context (GstEglAdaptationContext * ctx)
{
  EAGLContext *context;
  context = [[EAGLContext alloc] initWithAPI:kEAGLRenderingAPIOpenGLES2];
  if (context == nil)
    GST_ERROR_OBJECT (ctx->element, "Failed to create EAGL GLES2 context");
    return FALSE;
  }
  ctx->eagl_context = context;

  return TRUE;
}

gboolean
gst_egl_adaptation_context_make_current (GstEglAdaptationContext * ctx,
    gboolean bind)
{
  if (bind && ctx->eagl_context) {
    EAGLContext *cur_ctx = [EAGLContext currentContext];

    if (cur_ctx == ctx->eagl_context) {
      GST_DEBUG_OBJECT (ctx->element,
          "Already attached the context to thread %p", g_thread_self ());
      return TRUE;
    }

    GST_DEBUG_OBJECT (ctx->element, "Attaching context to thread %p",
        g_thread_self ());
    if ([EAGLContext setCurrentContext: ctx->eagl_context] == NO) {
      got_egl_error ("eglMakeCurrent");
      GST_ERROR_OBJECT (ctx->element, "Couldn't bind context");
      return FALSE;
    }
  } else {
    GST_DEBUG_OBJECT (ctx->element, "Detaching context from thread %p",
        g_thread_self ());
    if ([EAGLContext setCurrentContext: nil] == NO) {
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
  GLuint framebuffer;
  GLuint colorRenderbuffer;
  GLint width;
  GLint height;
  GLuint depthRenderbuffer;
  GLenum status;

  /* Allocate framebuffer */
  glGenFramebuffers(1, &framebuffer);
  glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
 
  /* Allocate color render buffer */
  glGenRenderbuffers(1, &colorRenderbuffer);
  glBindRenderbuffer(GL_RENDERBUFFER, colorRenderbuffer);
  [ctx->eagl_context renderbufferStorage:GL_RENDERBUFFER fromDrawable:myEAGLLayer];
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
      GL_RENDERBUFFER, colorRenderbuffer);

  /* Get renderbuffer width/height */
  glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_WIDTH, &width);
  glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_HEIGHT, &height);

  /* allocate depth render buffer */
  glGenRenderbuffers(1, &depthRenderbuffer);
  glBindRenderbuffer(GL_RENDERBUFFER, depthRenderbuffer);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, width, height);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
  GL_RENDERBUFFER, depthRenderbuffer);

  /* check creation status */
  status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  if(status != GL_FRAMEBUFFER_COMPLETE) {
    NSLog(@"failed to make complete framebuffer object %x", status);
    return FALSE;
  }

  ctx->framebuffer = framebuffer;
  ctx->color_renderbuffer = colorRenderbuffer;

  return TRUE;
}

gboolean
_gst_egl_choose_config (GstEglAdaptationContext * ctx, gboolean try_only, gint * num_configs)
{
  CAEAGLLayer *eaglLayer = (CAEAGLLayer *)[[UIView ctx->window] layer];
  NSDictionary *dict = [NSDictionary dictionaryWithObjectsAndKeys:
                       [NSNumber numberWithBool:NO], kEAGLDrawablePropertyRetainedBacking,
                        kEAGLColorFormatRGBA8888, kEAGLDrawablePropertyColorFormat,
                        nil];
  [eaglLayer setOpaque:YES];
  [eaglLayer setDrawableProperties:dict];

  if (num_configs)
    *num_configs = 1;
}

void
gst_egl_adaptation_query_buffer_preserved (GstEglAdaptationContext * ctx)
{
  CAEAGLLayer *eaglLayer = (CAEAGLLayer *)[[UIView ctx->window] layer];
  NSDictionary *dict = [eaglLayer drawableProperties];

  ctx->buffer_preserved = FALSE;
  if ([dict objectForKey: kEAGLDrawablePropertyRetainedBacking] != nil) {
    NSNumber n = [dict objectForKey: kEAGLDrawablePropertyRetainedBacking];
    ctx->buffer_preserved = n != [NSNumber numberWithBool:NO];
  } else {
    GST_DEBUG_OBJECT (ctx->element, "No information about buffer preserving in layer properties");
  }
}

void
gst_egl_adaptation_query_par (GstEglAdaptationContext * ctx)
{
  /* TODO how can we check this? */
  ctx->pixel_aspect_ratio = 1;
  ctx->pixel_aspect_ratio_d = 1;
}

gboolean
gst_egl_adaptation_context_update_surface_dimensions (GstEglAdaptationContext *
    ctx)
{
  GLint width;
  GLint height;

  /* Get renderbuffer width/height */
  glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_WIDTH, &width);
  glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_HEIGHT, &height);

  if (width != ctx->surface_width || height != ctx->surface_height) {
    ctx->surface_width = width;
    ctx->surface_height = height;
    GST_INFO_OBJECT (ctx->element, "Got surface of %dx%d pixels", width,
        height);
    return TRUE;
  }

  return FALSE;
}

void
gst_egl_adaptation_context_init_egl_exts (GstEglAdaptationContext * ctx)
{
  NSString *extensionsString = [NSString stringWithCString:glGetString(GL_EXTENSIONS) encoding: NSASCIIStringEncoding];

  GST_DEBUG_OBJECT (ctx->element, "Available GL extensions: %s\n",
      GST_STR_NULL ([extensionsString UTF8String]));
}

void
gst_egl_adaptation_destroy_surface (GstEglAdaptationContext * ctx)
{
  if (ctx->framebuffer) {
    glDeleteFrameuffers (1, &ctx->framebuffer);
    ctx->framebuffer = 0;
    ctx->have_surface = FALSE;
  }
}

void
gst_egl_adaptation_destroy_context (GstEglAdaptationContext * ctx)
{
  if (ctx->eglglesctx.eglcontext) {
    [ctx->eagl_context dealloc];
    ctx->eagl_context = NULL;
  }
}

gboolean
gst_egl_adaptation_context_swap_buffers (GstEglAdaptationContext * ctx)
{
  glBindRenderbuffer(GL_RENDERBUFFER, ctx->colorRenderbuffer);
  [context presentRenderbuffer:GL_RENDERBUFFER];

  return TRUE;
}

