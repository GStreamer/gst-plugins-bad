/* GStreamer
 * Copyright (C) 2020 Igalia, S.L.
 *     Author: Víctor Jáquez <vjaquez@igalia.com>
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

#pragma once

#include "gstvadisplay.h"

G_BEGIN_DECLS

typedef struct _GstVaDecodePicture GstVaDecodePicture;
struct _GstVaDecodePicture
{
  GArray *buffers;
  GArray *slices;
  GstBuffer *gstbuffer;
};

#define GST_TYPE_VA_DECODER (gst_va_decoder_get_type())
G_DECLARE_FINAL_TYPE (GstVaDecoder, gst_va_decoder, GST, VA_DECODER, GstObject)

GstVaDecoder *        gst_va_decoder_new                  (GstVaDisplay * display,
                                                           guint32 codec);
gboolean              gst_va_decoder_open                 (GstVaDecoder * self,
                                                           VAProfile profile,
                                                           guint rt_format);
gboolean              gst_va_decoder_close                (GstVaDecoder * self);
gboolean              gst_va_decoder_is_open              (GstVaDecoder * self);
gboolean              gst_va_decoder_set_format           (GstVaDecoder * self,
                                                           gint coded_width,
                                                           gint coded_height,
                                                           GArray * surfaces);
GstCaps *             gst_va_decoder_get_srcpad_caps      (GstVaDecoder * self);
GstCaps *             gst_va_decoder_get_sinkpad_caps     (GstVaDecoder * self);
gboolean              gst_va_decoder_has_profile          (GstVaDecoder * self,
							   VAProfile profile);
gint                  gst_va_decoder_get_mem_types        (GstVaDecoder * self);
GArray *              gst_va_decoder_get_surface_formats  (GstVaDecoder * self);

gboolean              gst_va_decoder_add_param_buffer     (GstVaDecoder * self,
							   GstVaDecodePicture * pic,
							   gint type,
							   gpointer data,
							   gsize size);
gboolean              gst_va_decoder_add_slice_buffer     (GstVaDecoder * self,
							   GstVaDecodePicture * pic,
							   gpointer params_data,
							   gsize params_size,
							   gpointer slice_data,
							   gsize slice_size);
gboolean              gst_va_decoder_decode               (GstVaDecoder * self,
                                                           GstVaDecodePicture * pic);
gboolean              gst_va_decoder_destroy_buffers      (GstVaDecoder * self,
                                                           GstVaDecodePicture * pic);

GstVaDecodePicture *  gst_va_decode_picture_new           (GstBuffer * buffer);
VASurfaceID           gst_va_decode_picture_get_surface   (GstVaDecodePicture * pic);
void                  gst_va_decode_picture_free          (GstVaDecodePicture * pic);

gboolean              gst_va_decoder_format_changed       (GstVaDecoder * decoder,
                                                           VAProfile new_profile,
                                                           guint new_rtformat,
                                                           gint new_width,
                                                           gint new_height);

G_END_DECLS
