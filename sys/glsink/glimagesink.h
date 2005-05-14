/* GStreamer
 * Copyright (C) <2003> Julien Moutte <julien@moutte.net>
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
 
#ifndef __GST_GLIMAGESINK_H__
#define __GST_GLIMAGESINK_H__

#include <gst/video/videosink.h>

#include <GL/glx.h>
#include <GL/gl.h>
#include <GL/glu.h>

#include <string.h>
#include <math.h>

G_BEGIN_DECLS

#define GST_TYPE_GLIMAGESINK \
  (gst_glimagesink_get_type())
#define GST_GLIMAGESINK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_GLIMAGESINK, GstGLImageSink))
#define GST_GLIMAGESINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_GLIMAGESINK, GstGLImageSink))
#define GST_IS_GLIMAGESINK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_GLIMAGESINK))
#define GST_IS_GLIMAGESINK_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_GLIMAGESINK))

typedef struct _GstGLImageSink GstGLImageSink;
typedef struct _GstGLImageSinkClass GstGLImageSinkClass;

struct _GstGLImageSink {
  /* Our element stuff */
  GstVideoSink videosink;

  char *display_name;
  
  Window window;
  Window parent_window;
  XVisualInfo *visinfo;
  
  gdouble framerate;
  
  gint pixel_width, pixel_height;  /* Unused */
 
  GstClockTime time;
  
  Display *display;
  GLXContext context;

  int max_texture_size;
  gboolean have_yuv;

  gboolean use_rgb;
  gboolean use_rgbx;
  gboolean use_yuy2;
#if 0
  guint pointer_x, pointer_y;
  gboolean pointer_moved;
  gboolean pointer_button[5];

  gboolean synchronous;
  gboolean signal_handoffs;  
#endif
};

struct _GstGLImageSinkClass {
  GstVideoSinkClass parent_class;
};

GType gst_glimagesink_get_type(void);

G_END_DECLS

#endif /* __GST_GLIMAGESINK_H__ */
