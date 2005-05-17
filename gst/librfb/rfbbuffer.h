
#ifndef __RFB_BUFFER_H__
#define __RFB_BUFFER_H__

#include <glib.h>

typedef struct _RfbBuffer RfbBuffer;
typedef struct _RfbBufferQueue RfbBufferQueue;

struct _RfbBuffer
{
  unsigned char *data;
  int length;

  int ref_count;

  RfbBuffer *parent;

  void (*free) (RfbBuffer *, void *);
  void *priv;
};

struct _RfbBufferQueue
{
  GList *buffers;
  int depth;
  int offset;
};

RfbBuffer *rfb_buffer_new (void);
RfbBuffer *rfb_buffer_new_and_alloc (int size);
RfbBuffer *rfb_buffer_new_with_data (void *data, int size);
RfbBuffer *rfb_buffer_new_subbuffer (RfbBuffer * buffer, int offset,
    int length);
void rfb_buffer_ref (RfbBuffer * buffer);
void rfb_buffer_unref (RfbBuffer * buffer);

RfbBufferQueue *rfb_buffer_queue_new (void);
void rfb_buffer_queue_free (RfbBufferQueue * queue);
int rfb_buffer_queue_get_depth (RfbBufferQueue * queue);
int rfb_buffer_queue_get_offset (RfbBufferQueue * queue);
void rfb_buffer_queue_push (RfbBufferQueue * queue,
    RfbBuffer * buffer);
RfbBuffer *rfb_buffer_queue_pull (RfbBufferQueue * queue, int len);
RfbBuffer *rfb_buffer_queue_peek (RfbBufferQueue * queue, int len);

#endif
