/*  Copyright (C) 2015-2016 Setplex. All right reserved.
    This file is part of Rixjob.
    Rixjob is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
    Rixjob is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
    You should have received a copy of the GNU General Public License
    along with Rixjob.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef _GST_NVH264DEC_H_
#define _GST_NVH264DEC_H_

#include <gst/video/video.h>
#include <gst/video/gstvideodecoder.h>

G_BEGIN_DECLS
#define GST_TYPE_NVH264DEC (gst_nvh264dec_get_type())
#define GST_NVH264DEC(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_NVH264DEC, GstNvh264dec))
#define GST_NVH264DEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_NVH264DEC, GstNvh264decClass))
#define GST_IS_NVH264DEC(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_NVH264DEC))
#define GST_IS_NVH264DEC_CLASS(obj) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_NVH264DEC))

typedef struct _GstNvh264dec GstNvh264dec;
typedef struct _GstNvh264decClass GstNvh264decClass;
typedef struct _GstNvh264decPrivate GstNvh264decPrivate;

struct _GstNvh264dec {
  GstVideoDecoder base_nvh264dec;

  /*< private >*/
  GstNvh264decPrivate* priv;
};

struct _GstNvh264decClass {
  GstVideoDecoderClass base_nvh264dec_class;
};

GType gst_nvh264dec_get_type(void);

G_END_DECLS
#endif
