/* GStreamer
 * Copyright (C) 2005 Wim Taymans <wim@fluendo.com>
 *
 * gstringbuffer.c: 
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

#include <string.h>

#include "gstringbuffer.h"

static void gst_ringbuffer_class_init (GstRingBufferClass * klass);
static void gst_ringbuffer_init (GstRingBuffer * ringbuffer);
static void gst_ringbuffer_dispose (GObject * object);
static void gst_ringbuffer_finalize (GObject * object);

static GstObjectClass *parent_class = NULL;

/* ringbuffer abstract base class */
GType
gst_ringbuffer_get_type (void)
{
  static GType ringbuffer_type = 0;

  if (!ringbuffer_type) {
    static const GTypeInfo ringbuffer_info = {
      sizeof (GstRingBufferClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_ringbuffer_class_init,
      NULL,
      NULL,
      sizeof (GstRingBuffer),
      0,
      (GInstanceInitFunc) gst_ringbuffer_init,
      NULL
    };

    ringbuffer_type = g_type_register_static (GST_TYPE_OBJECT, "GstRingBuffer",
        &ringbuffer_info, G_TYPE_FLAG_ABSTRACT);
  }
  return ringbuffer_type;
}

static void
gst_ringbuffer_class_init (GstRingBufferClass * klass)
{
  GObjectClass *gobject_class;
  GstObjectClass *gstobject_class;

  gobject_class = (GObjectClass *) klass;
  gstobject_class = (GstObjectClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_OBJECT);

  gobject_class->dispose = GST_DEBUG_FUNCPTR (gst_ringbuffer_dispose);
  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_ringbuffer_finalize);
}

static void
gst_ringbuffer_init (GstRingBuffer * ringbuffer)
{
  ringbuffer->acquired = FALSE;
  ringbuffer->state = GST_RINGBUFFER_STATE_STOPPED;
  ringbuffer->playseg = 0;
  ringbuffer->writeseg = 1;
  ringbuffer->segfilled = 0;
  ringbuffer->waiters = FALSE;
  ringbuffer->cond = g_cond_new ();
}

static void
gst_ringbuffer_dispose (GObject * object)
{
  GstRingBuffer *ringbuffer = GST_RINGBUFFER (object);

  G_OBJECT_CLASS (parent_class)->dispose (G_OBJECT (ringbuffer));
}

static void
gst_ringbuffer_finalize (GObject * object)
{
  GstRingBuffer *ringbuffer = GST_RINGBUFFER (object);

  g_cond_free (ringbuffer->cond);

  G_OBJECT_CLASS (parent_class)->finalize (G_OBJECT (ringbuffer));
}

void
gst_ringbuffer_set_callback (GstRingBuffer * buf, GstRingBufferCallback cb,
    gpointer data)
{
  GST_LOCK (buf);
  buf->callback = cb;
  buf->cb_data = data;
  GST_UNLOCK (buf);
}


gboolean
gst_ringbuffer_acquire (GstRingBuffer * buf, GstRingBufferSpec * spec)
{
  gboolean res = FALSE;
  GstRingBufferClass *rclass;

  GST_LOCK (buf);
  if (buf->acquired) {
    res = TRUE;
    goto done;
  }
  buf->acquired = TRUE;
  GST_UNLOCK (buf);

  rclass = GST_RINGBUFFER_GET_CLASS (buf);
  if (rclass->acquire)
    res = rclass->acquire (buf, spec);

  GST_LOCK (buf);
  if (!res) {
    buf->acquired = FALSE;
  }
done:
  GST_UNLOCK (buf);

  return res;
}

gboolean
gst_ringbuffer_release (GstRingBuffer * buf)
{
  gboolean res = FALSE;
  GstRingBufferClass *rclass;

  GST_LOCK (buf);
  if (!buf->acquired) {
    res = TRUE;
    goto done;
  }
  buf->acquired = FALSE;
  GST_UNLOCK (buf);

  rclass = GST_RINGBUFFER_GET_CLASS (buf);
  if (rclass->release)
    res = rclass->release (buf);

  GST_LOCK (buf);
  if (!res) {
    buf->acquired = TRUE;
  }
done:
  GST_UNLOCK (buf);

  return res;
}

gboolean
gst_ringbuffer_play (GstRingBuffer * buf)
{
  gboolean res = FALSE;
  GstRingBufferClass *rclass;

  GST_LOCK (buf);
  if (buf->state == GST_RINGBUFFER_STATE_PLAYING) {
    res = TRUE;
    goto done;
  }
  buf->state = GST_RINGBUFFER_STATE_PLAYING;

  rclass = GST_RINGBUFFER_GET_CLASS (buf);
  if (rclass->play)
    res = rclass->play (buf);

  if (!res) {
    buf->state = GST_RINGBUFFER_STATE_STOPPED;
  }
done:
  GST_UNLOCK (buf);

  return res;
}

gboolean
gst_ringbuffer_stop (GstRingBuffer * buf)
{
  gboolean res = FALSE;
  GstRingBufferClass *rclass;

  GST_LOCK (buf);
  if (buf->state == GST_RINGBUFFER_STATE_STOPPED) {
    res = TRUE;
    goto done;
  }
  buf->state = GST_RINGBUFFER_STATE_STOPPED;

  rclass = GST_RINGBUFFER_GET_CLASS (buf);
  if (rclass->stop)
    res = rclass->stop (buf);

  if (!res) {
    buf->state = GST_RINGBUFFER_STATE_PLAYING;
  }
done:
  GST_UNLOCK (buf);

  return res;
}

void
gst_ringbuffer_callback (GstRingBuffer * buf, guint advance)
{
  GST_LOCK (buf);
  buf->playseg = (buf->playseg + advance) % buf->spec.segtotal;
  if (buf->playseg == buf->writeseg) {
    g_print ("underrun!! read %d, write %d\n", buf->playseg, buf->writeseg);
    buf->writeseg = (buf->playseg + 1) % buf->spec.segtotal;
    buf->segfilled = 0;
  }
  if (buf->waiters)
    GST_RINGBUFFER_SIGNAL (buf);
  GST_UNLOCK (buf);

  if (buf->callback)
    buf->callback (buf, advance, buf->cb_data);
}

guint
gst_ringbuffer_write (GstRingBuffer * buf, GstClockTime time, guchar * data,
    guint len)
{
  guint towrite = len;
  guint written = 0;

  GST_LOCK (buf);
  /* we write the complete buffer */
  while (towrite > 0) {
    guint segavail;
    guint segwrite;

    /* we cannot write anymore since the buffer is filled, wait for 
     * some space to become available */
    while (buf->writeseg == buf->playseg) {
      buf->waiters = TRUE;
      GST_RINGBUFFER_WAIT (buf);
      buf->waiters = FALSE;
    }

    /* this is the available size now in the current segment */
    segavail = buf->spec.segsize - buf->segfilled;

    /* we write up to the available space */
    segwrite = MIN (segavail, towrite);
    memcpy (GST_BUFFER_DATA (buf->data) + buf->writeseg * buf->spec.segsize +
        buf->segfilled, data, segwrite);
    towrite -= segwrite;
    data += segwrite;
    buf->segfilled += segwrite;
    written += segwrite;
    /* we wrote a complete segment, advance the write pointer */
    if (buf->segfilled == buf->spec.segsize) {
      buf->writeseg = (buf->writeseg + 1) % buf->spec.segtotal;
      buf->segfilled = 0;
    }
  }
  GST_UNLOCK (buf);

  return written;
}
