/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2005 Wim Taymans <wim@fluendo.com>
 *
 * gstrinbuffer.h: 
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

#ifndef __GST_RINGBUFFER_H__
#define __GST_RINGBUFFER_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_RINGBUFFER  	         (gst_ringbuffer_get_type())
#define GST_RINGBUFFER(obj) 		 (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_RINGBUFFER,GstRingBuffer))
#define GST_RINGBUFFER_CLASS(klass) 	 (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_RINGBUFFER,GstRingBufferClass))
#define GST_RINGBUFFER_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_RINGBUFFER, GstRingBufferClass))
#define GST_IS_RINGBUFFER(obj)  	 (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_RINGBUFFER))
#define GST_IS_RINGBUFFER_CLASS(obj)     (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_RINGBUFFER))

typedef struct _GstRingBuffer GstRingBuffer;
typedef struct _GstRingBufferClass GstRingBufferClass;
typedef struct _GstRingBufferSpec GstRingBufferSpec;

typedef void (*GstRingBufferCallback) (GstRingBuffer *rbuf, guint advance, gpointer data);

typedef enum {
  GST_RINGBUFFER_STATE_STOPPED,
  GST_RINGBUFFER_STATE_PLAYING,
} GstRingBufferState;

typedef enum {
  GST_SEGSTATE_INVALID,
  GST_SEGSTATE_EMPTY,
  GST_SEGSTATE_FILLED,
} GstRingBufferSegState;

struct _GstRingBufferSpec
{
  GstCaps  *caps;

  guint     segsize;
  guint     segtotal;
};
#define GST_RINGBUFFER_GET_COND(buf) (((GstRingBuffer *)buf)->cond)
#define GST_RINGBUFFER_WAIT(buf)     (g_cond_wait (GST_RINGBUFFER_GET_COND (buf), GST_GET_LOCK (buf)))
#define GST_RINGBUFFER_SIGNAL(buf)   (g_cond_signal (GST_RINGBUFFER_GET_COND (buf)))

struct _GstRingBuffer {
  GstObject 	         object;

  /*< public >*/ /* with LOCK */
  GCond                 *cond;
  gboolean               acquired;
  GstRingBufferState     state;
  GstBuffer             *data;
  GstRingBufferSpec      spec;
  GstRingBufferSegState *segstate;

  gboolean		 waiters;
  gint                   playseg;
  gint                   writeseg;
  gint                   segfilled;

  GstRingBufferCallback  callback;
  gpointer               cb_data;
};

struct _GstRingBufferClass {
  GstObjectClass parent_class;

  /*< public >*/
  gboolean     (*acquire)      (GstRingBuffer *buf, GstRingBufferSpec *spec);
  gboolean     (*release)      (GstRingBuffer *buf);

  gboolean     (*play)         (GstRingBuffer *buf);
  gboolean     (*stop)         (GstRingBuffer *buf);
};

GType gst_ringbuffer_get_type(void);

void     	gst_ringbuffer_set_callback   	(GstRingBuffer *buf, GstRingBufferCallback cb, 
		                        	 gpointer data);

gboolean 	gst_ringbuffer_acquire 		(GstRingBuffer *buf, GstRingBufferSpec *spec);
gboolean 	gst_ringbuffer_release 		(GstRingBuffer *buf);

gboolean 	gst_ringbuffer_play 		(GstRingBuffer *buf);
gboolean 	gst_ringbuffer_stop 		(GstRingBuffer *buf);

guint 		gst_ringbuffer_write 		(GstRingBuffer *buf, GstClockTime time, 
						 guchar *data, guint len);

void	 	gst_ringbuffer_callback 	(GstRingBuffer *buf, guint advance);

G_END_DECLS

#endif /* __GST_RINGBUFFER_H__ */
