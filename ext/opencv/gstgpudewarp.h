/*
 * GStreamer
 * Copyright (C) 2016 Prassel S.r.l
 *  Author: Nicola Murino <nicola.murino@gmail.com>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef __GST_DEWARP_H__
#define __GST_DEWARP_H__

#include <gst/gst.h>
#include <gst/opencv/gstopencvvideofilter.h>
#include <opencv2/core/core.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/gpu/gpu.hpp>

G_BEGIN_DECLS
/* #defines don't like whitespacey bits */
#define GST_TYPE_DEWARP \
  (gst_gpu_dewarp_get_type())
#define GST_DEWARP(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_DEWARP,GstGpuDewarp))
#define GST_DEWARP_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_DEWARP,GstGpuDewarpClass))
#define GST_IS_DEWARP(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_DEWARP))
#define GST_IS_DEWARP_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_DEWARP))
typedef struct _GstGpuDewarp GstGpuDewarp;
typedef struct _GstGpuDewarpClass GstGpuDewarpClass;

enum _GstGpuDewarpDisplayMode {
  GST_DEWARP_DISPLAY_PANORAMA = 0,
  GST_DEWARP_DISPLAY_DOUBLE_PANORAMA = 1,
  GST_DEWARP_DISPLAY_QUAD_VIEW = 2
};

enum _GstGpuDewarpInterpolationMode {
  GST_DEWARP_INTER_NEAREST = 0,
  GST_DEWARP_INTER_LINEAR = 1,
  GST_DEWARP_INTER_CUBIC = 2,
  GST_DEWARP_INTER_LANCZOS4 = 3
};

struct _GstGpuDewarp
{
  GstOpencvVideoFilter element;
  cv::gpu::GpuMat *map_x;
  cv::gpu::GpuMat *map_y;
  gdouble x_center;
  gdouble y_center;
  gdouble inner_radius;
  gdouble outer_radius;
  gdouble remap_correction_x;
  gdouble remap_correction_y;
  gboolean need_map_update;
  gint pad_sink_width;
  gint pad_sink_height;
  gint in_width;
  gint in_height;
  gint out_width;
  gint out_height;
  gint display_mode;
  gint interpolation_mode;
};

struct _GstGpuDewarpClass
{
  GstOpencvVideoFilterClass parent_class;
};

GType gst_gpu_dewarp_get_type (void);

gboolean gst_gpu_dewarp_plugin_init (GstPlugin * plugin);

G_END_DECLS
#endif /* __GST_DEWARP_H__ */

