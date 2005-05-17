
#ifndef HAVE_CONFIG_H
#include "config.h"
#endif

#include <rfbbuffer.h>
#include <glib.h>
#include <string.h>
#include <gst/gst.h>

static void rfb_buffer_free_mem (RfbBuffer * buffer, void *);
static void rfb_buffer_free_subbuffer (RfbBuffer * buffer, void *priv);

#define oil_copy_u8(a,b,c) memcpy(a,b,c)

RfbBuffer *
rfb_buffer_new (void)
{
  RfbBuffer *buffer;

  buffer = g_new0 (RfbBuffer, 1);
  buffer->ref_count = 1;
  return buffer;
}

RfbBuffer *
rfb_buffer_new_and_alloc (int size)
{
  RfbBuffer *buffer = rfb_buffer_new ();

  buffer->data = g_malloc (size);
  buffer->length = size;
  buffer->free = rfb_buffer_free_mem;

  return buffer;
}

RfbBuffer *
rfb_buffer_new_with_data (void *data, int size)
{
  RfbBuffer *buffer = rfb_buffer_new ();

  buffer->data = data;
  buffer->length = size;
  buffer->free = rfb_buffer_free_mem;

  return buffer;
}

RfbBuffer *
rfb_buffer_new_subbuffer (RfbBuffer * buffer, int offset, int length)
{
  RfbBuffer *subbuffer = rfb_buffer_new ();

  if (buffer->parent) {
    rfb_buffer_ref (buffer->parent);
    subbuffer->parent = buffer->parent;
  } else {
    rfb_buffer_ref (buffer);
    subbuffer->parent = buffer;
  }
  subbuffer->data = buffer->data + offset;
  subbuffer->length = length;
  subbuffer->free = rfb_buffer_free_subbuffer;

  return subbuffer;
}

void
rfb_buffer_ref (RfbBuffer * buffer)
{
  buffer->ref_count++;
}

void
rfb_buffer_unref (RfbBuffer * buffer)
{
  buffer->ref_count--;
  if (buffer->ref_count == 0) {
    if (buffer->free)
      buffer->free (buffer, buffer->priv);
    g_free (buffer);
  }
}

static void
rfb_buffer_free_mem (RfbBuffer * buffer, void *priv)
{
  g_free (buffer->data);
}

static void
rfb_buffer_free_subbuffer (RfbBuffer * buffer, void *priv)
{
  rfb_buffer_unref (buffer->parent);
}


RfbBufferQueue *
rfb_buffer_queue_new (void)
{
  return g_new0 (RfbBufferQueue, 1);
}

int
rfb_buffer_queue_get_depth (RfbBufferQueue * queue)
{
  return queue->depth;
}

int
rfb_buffer_queue_get_offset (RfbBufferQueue * queue)
{
  return queue->offset;
}

void
rfb_buffer_queue_free (RfbBufferQueue * queue)
{
  GList *g;

  for (g = g_list_first (queue->buffers); g; g = g_list_next (g)) {
    rfb_buffer_unref ((RfbBuffer *) g->data);
  }
  g_list_free (queue->buffers);
  g_free (queue);
}

void
rfb_buffer_queue_push (RfbBufferQueue * queue, RfbBuffer * buffer)
{
  queue->buffers = g_list_append (queue->buffers, buffer);
  queue->depth += buffer->length;
}

RfbBuffer *
rfb_buffer_queue_pull (RfbBufferQueue * queue, int length)
{
  GList *g;
  RfbBuffer *newbuffer;
  RfbBuffer *buffer;
  RfbBuffer *subbuffer;

  g_return_val_if_fail (length > 0, NULL);

  if (queue->depth < length) {
    return NULL;
  }

  GST_LOG ("pulling %d, %d available", length, queue->depth);

  g = g_list_first (queue->buffers);
  buffer = g->data;

  if (buffer->length > length) {
    newbuffer = rfb_buffer_new_subbuffer (buffer, 0, length);

    subbuffer = rfb_buffer_new_subbuffer (buffer, length,
        buffer->length - length);
    g->data = subbuffer;
    rfb_buffer_unref (buffer);
  } else {
    int offset = 0;

    newbuffer = rfb_buffer_new_and_alloc (length);

    while (offset < length) {
      g = g_list_first (queue->buffers);
      buffer = g->data;

      if (buffer->length > length - offset) {
        int n = length - offset;

        oil_copy_u8 (newbuffer->data + offset, buffer->data, n);
        subbuffer = rfb_buffer_new_subbuffer (buffer, n, buffer->length - n);
        g->data = subbuffer;
        rfb_buffer_unref (buffer);
        offset += n;
      } else {
        oil_copy_u8 (newbuffer->data + offset, buffer->data, buffer->length);

        queue->buffers = g_list_delete_link (queue->buffers, g);
        offset += buffer->length;
      }
    }
  }

  queue->depth -= length;
  queue->offset += length;

  return newbuffer;
}

RfbBuffer *
rfb_buffer_queue_peek (RfbBufferQueue * queue, int length)
{
  GList *g;
  RfbBuffer *newbuffer;
  RfbBuffer *buffer;
  int offset = 0;

  g_return_val_if_fail (length > 0, NULL);

  if (queue->depth < length) {
    return NULL;
  }

  GST_LOG ("peeking %d, %d available", length, queue->depth);

  g = g_list_first (queue->buffers);
  buffer = g->data;
  if (buffer->length > length) {
    newbuffer = rfb_buffer_new_subbuffer (buffer, 0, length);
  } else {
    newbuffer = rfb_buffer_new_and_alloc (length);
    while (offset < length) {
      buffer = g->data;

      if (buffer->length > length - offset) {
        int n = length - offset;

        oil_copy_u8 (newbuffer->data + offset, buffer->data, n);
        offset += n;
      } else {
        oil_copy_u8 (newbuffer->data + offset, buffer->data, buffer->length);
        offset += buffer->length;
      }
      g = g_list_next (g);
    }
  }

  return newbuffer;
}
