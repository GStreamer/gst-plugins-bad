
#include <rfb.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

#include <gst/gst.h>


RfbDecoder *
rfb_decoder_new (void)
{
  RfbDecoder *decoder = g_new0 (RfbDecoder, 1);

  decoder->queue = rfb_buffer_queue_new ();

  return decoder;
}

void
rfb_decoder_connect_tcp (RfbDecoder * decoder, char *addr, unsigned int port)
{
  int fd;
  struct sockaddr_in sa;

  fd = socket (PF_INET, SOCK_STREAM, 0);

  sa.sin_family = AF_INET;
  sa.sin_port = htons (port);
  inet_pton (AF_INET, addr, &sa.sin_addr);
  connect (fd, (struct sockaddr *) &sa, sizeof (struct sockaddr));

  decoder->fd = fd;
}


static gboolean rfb_decoder_state_wait_for_protocol_version (RfbDecoder *
    decoder);
static gboolean rfb_decoder_state_wait_for_security (RfbDecoder * decoder);
static gboolean rfb_decoder_state_send_client_initialisation (RfbDecoder *
    decoder);
static gboolean rfb_decoder_state_wait_for_server_initialisation (RfbDecoder *
    decoder);
static gboolean rfb_decoder_state_normal (RfbDecoder * decoder);
static gboolean rfb_decoder_state_framebuffer_update (RfbDecoder * decoder);
static gboolean rfb_decoder_state_framebuffer_update_rectangle (RfbDecoder *
    decoder);
static gboolean rfb_decoder_state_set_colour_map_entries (RfbDecoder * decoder);
static gboolean rfb_decoder_state_server_cut_text (RfbDecoder * decoder);

gboolean
rfb_decoder_iterate (RfbDecoder * decoder)
{
  g_return_val_if_fail (decoder != NULL, FALSE);

  if (decoder->state == NULL) {
    decoder->state = rfb_decoder_state_wait_for_protocol_version;
  }

  GST_DEBUG ("iterating...");

  return decoder->state (decoder);
}

#define RFB_GET_UINT32(ptr) GUINT32_FROM_BE (*(guint32 *)(ptr))
#define RFB_GET_UINT16(ptr) GUINT16_FROM_BE (*(guint16 *)(ptr))
#define RFB_GET_UINT8(ptr) (*(guint8 *)(ptr))

#define RFB_SET_UINT32(ptr, val) (*(guint32 *)(ptr) = GUINT32_TO_BE (val))
#define RFB_SET_UINT16(ptr, val) (*(guint16 *)(ptr) = GUINT16_TO_BE (val))
#define RFB_SET_UINT8(ptr, val) (*(guint8 *)(ptr) = val)

static gboolean
rfb_decoder_state_wait_for_protocol_version (RfbDecoder * decoder)
{
  RfbBuffer *buffer;
  guint8 *data;

  GST_DEBUG ("enter");

  buffer = rfb_buffer_queue_pull (decoder->queue, 12);
  if (!buffer)
    return FALSE;

  data = buffer->data;

  GST_DEBUG ("\"%.11s\"", buffer->data);
  if (memcmp (buffer->data, "RFB 003.00", 10) != 0) {
    decoder->error_msg = g_strdup ("bad version string from server");
    return FALSE;
  }

  decoder->protocol_minor = RFB_GET_UINT8 (buffer->data + 10) - '0';
  if (decoder->protocol_minor != 3 && decoder->protocol_minor != 7) {
    decoder->error_msg = g_strdup ("bad version number from server");
    return FALSE;
  }
  rfb_buffer_unref (buffer);

  if (decoder->protocol_minor == 3) {
    rfb_decoder_send (decoder, (guchar *) "RFB 003.003\n", 12);
  } else {
    rfb_decoder_send (decoder, (guchar *) "RFB 003.007\n", 12);
  }

  decoder->state = rfb_decoder_state_wait_for_security;

  return TRUE;
}

static gboolean
rfb_decoder_state_wait_for_security (RfbDecoder * decoder)
{
  RfbBuffer *buffer;
  int n;
  int i;

  GST_DEBUG ("enter");

  if (decoder->protocol_minor == 3) {
    buffer = rfb_buffer_queue_pull (decoder->queue, 4);
    if (!buffer)
      return FALSE;

    decoder->security_type = RFB_GET_UINT32 (buffer->data);
    GST_DEBUG ("security = %d", decoder->security_type);

    if (decoder->security_type == 0) {
      decoder->error_msg = g_strdup ("connection failed");
    } else if (decoder->security_type == 2) {
      decoder->error_msg =
          g_strdup ("server asked for authentication, which is unsupported");
    }

    rfb_buffer_unref (buffer);

    decoder->state = rfb_decoder_state_send_client_initialisation;
    return TRUE;
  } else {
    guint8 reply;

    buffer = rfb_buffer_queue_peek (decoder->queue, 1);
    if (!buffer)
      return FALSE;

    n = RFB_GET_UINT8 (buffer->data);
    rfb_buffer_unref (buffer);

    GST_DEBUG ("n = %d", n);

    if (n > 0) {
      gboolean have_none = FALSE;

      buffer = rfb_buffer_queue_pull (decoder->queue, n + 1);

      for (i = 0; i < n; i++) {
        GST_DEBUG ("security = %d", RFB_GET_UINT8 (buffer->data + 1 + i));
        if (RFB_GET_UINT8 (buffer->data + 1 + i) == 1) {
          /* does the server allow no authentication? */
          have_none = TRUE;
        }
      }

      rfb_buffer_unref (buffer);

      if (!have_none) {
        decoder->error_msg =
            g_strdup ("server asked for authentication, which is unsupported");
        return FALSE;
      }

      reply = 1;
      rfb_decoder_send (decoder, &reply, 1);
    } else {
      g_critical ("FIXME");
      return FALSE;
    }

    decoder->state = rfb_decoder_state_send_client_initialisation;
    return TRUE;
  }
}

static gboolean
rfb_decoder_state_send_client_initialisation (RfbDecoder * decoder)
{
  guint8 shared_flag;

  GST_DEBUG ("enter");

  shared_flag = decoder->shared_flag;
  rfb_decoder_send (decoder, &shared_flag, 1);

  decoder->state = rfb_decoder_state_wait_for_server_initialisation;
  return TRUE;
}

static gboolean
rfb_decoder_state_wait_for_server_initialisation (RfbDecoder * decoder)
{
  RfbBuffer *buffer;
  guint8 *data;
  guint32 name_length;

  GST_DEBUG ("enter");

  buffer = rfb_buffer_queue_peek (decoder->queue, 24);
  if (!buffer)
    return FALSE;

  data = buffer->data;

  decoder->width = RFB_GET_UINT16 (data + 0);
  decoder->height = RFB_GET_UINT16 (data + 2);
  decoder->bpp = RFB_GET_UINT8 (data + 4);
  decoder->depth = RFB_GET_UINT8 (data + 5);
  decoder->big_endian = RFB_GET_UINT8 (data + 6);
  decoder->true_colour = RFB_GET_UINT8 (data + 7);
  decoder->red_max = RFB_GET_UINT16 (data + 8);
  decoder->green_max = RFB_GET_UINT16 (data + 10);
  decoder->blue_max = RFB_GET_UINT16 (data + 12);
  decoder->red_shift = RFB_GET_UINT8 (data + 14);
  decoder->green_shift = RFB_GET_UINT8 (data + 15);
  decoder->blue_shift = RFB_GET_UINT8 (data + 16);

  GST_DEBUG ("width: %d", decoder->width);
  GST_DEBUG ("height: %d", decoder->height);
  GST_DEBUG ("bpp: %d", decoder->bpp);
  GST_DEBUG ("depth: %d", decoder->depth);
  GST_DEBUG ("true color: %d", decoder->true_colour);
  GST_DEBUG ("big endian: %d", decoder->big_endian);

  GST_DEBUG ("red shift: %d", decoder->red_shift);
  GST_DEBUG ("red max: %d", decoder->red_max);
  GST_DEBUG ("blue shift: %d", decoder->blue_shift);
  GST_DEBUG ("blue max: %d", decoder->blue_max);
  GST_DEBUG ("green shift: %d", decoder->green_shift);
  GST_DEBUG ("green max: %d", decoder->green_max);

  name_length = RFB_GET_UINT32 (data + 20);
  rfb_buffer_unref (buffer);

  buffer = rfb_buffer_queue_pull (decoder->queue, 24 + name_length);
  if (!buffer)
    return FALSE;

  decoder->name = g_strndup ((char *) (buffer->data) + 24, name_length);
  GST_DEBUG ("name: %s", decoder->name);
  rfb_buffer_unref (buffer);

  decoder->state = rfb_decoder_state_normal;
  decoder->busy = FALSE;
  decoder->inited = TRUE;

  if (decoder->bpp == 8 && decoder->depth == 8 &&
      decoder->true_colour &&
      decoder->red_shift == 0 && decoder->red_max == 0x07 &&
      decoder->green_shift == 3 && decoder->green_max == 0x07 &&
      decoder->blue_shift == 6 && decoder->blue_max == 0x03) {
    decoder->image_format = RFB_DECODER_IMAGE_RGB332;
  } else if (decoder->bpp == 32 && decoder->depth == 24 &&
      decoder->true_colour && decoder->big_endian == FALSE &&
      decoder->red_shift == 16 && decoder->red_max == 0xff &&
      decoder->green_shift == 8 && decoder->green_max == 0xff &&
      decoder->blue_shift == 0 && decoder->blue_max == 0xff) {
    decoder->image_format = RFB_DECODER_IMAGE_xRGB;
  } else {
    decoder->error_msg = g_strdup_printf ("unsupported server image format");
    return FALSE;
  }

  return TRUE;
}

static gboolean
rfb_decoder_state_normal (RfbDecoder * decoder)
{
  RfbBuffer *buffer;
  int message_type;

  GST_DEBUG ("enter");

  buffer = rfb_buffer_queue_pull (decoder->queue, 1);
  if (!buffer)
    return FALSE;
  message_type = RFB_GET_UINT8 (buffer->data);

  decoder->busy = TRUE;

  switch (message_type) {
    case 0:
      decoder->state = rfb_decoder_state_framebuffer_update;
      break;
    case 1:
      decoder->state = rfb_decoder_state_set_colour_map_entries;
      break;
    case 2:
      /* bell, ignored */
      decoder->busy = FALSE;
      decoder->state = rfb_decoder_state_normal;
      break;
    case 3:
      decoder->state = rfb_decoder_state_server_cut_text;
      break;
    default:
      g_critical ("unknown message type %d", message_type);
  }

  rfb_buffer_unref (buffer);

  return TRUE;
}

static gboolean
rfb_decoder_state_framebuffer_update (RfbDecoder * decoder)
{
  RfbBuffer *buffer;

  GST_DEBUG ("enter");

  buffer = rfb_buffer_queue_pull (decoder->queue, 3);
  if (!buffer)
    return FALSE;

  decoder->n_rects = RFB_GET_UINT16 (buffer->data + 1);
  decoder->state = rfb_decoder_state_framebuffer_update_rectangle;

  return TRUE;
}

static gboolean
rfb_decoder_state_framebuffer_update_rectangle (RfbDecoder * decoder)
{
  RfbBuffer *buffer;
  int x, y, w, h;
  int encoding;
  int size;

  GST_DEBUG ("enter");

  buffer = rfb_buffer_queue_peek (decoder->queue, 12);
  if (!buffer)
    return FALSE;

  x = RFB_GET_UINT16 (buffer->data + 0);
  y = RFB_GET_UINT16 (buffer->data + 2);
  w = RFB_GET_UINT16 (buffer->data + 4);
  h = RFB_GET_UINT16 (buffer->data + 6);
  encoding = RFB_GET_UINT32 (buffer->data + 8);

  if (encoding != 0)
    g_critical ("unimplemented encoding\n");

  rfb_buffer_unref (buffer);

  size = w * h * (decoder->bpp / 8);
  buffer = rfb_buffer_queue_pull (decoder->queue, size + 12);
  if (!buffer)
    return FALSE;

  if (decoder->paint_rect) {
    decoder->paint_rect (decoder, x, y, w, h, buffer->data + 12);
  }

  rfb_buffer_unref (buffer);

  decoder->n_rects--;
  if (decoder->n_rects == 0) {
    decoder->busy = FALSE;
    decoder->state = rfb_decoder_state_normal;
  }
  return TRUE;
}

static gboolean
rfb_decoder_state_set_colour_map_entries (RfbDecoder * decoder)
{
  g_critical ("not implemented");

  return FALSE;
}

static gboolean
rfb_decoder_state_server_cut_text (RfbDecoder * decoder)
{
  g_critical ("not implemented");

  return FALSE;
}


void
rfb_decoder_send_update_request (RfbDecoder * decoder,
    gboolean incremental, int x, int y, int width, int height)
{
  guint8 data[10];

  data[0] = 3;
  data[1] = incremental;
  RFB_SET_UINT16 (data + 2, x);
  RFB_SET_UINT16 (data + 4, y);
  RFB_SET_UINT16 (data + 6, width);
  RFB_SET_UINT16 (data + 8, height);

  rfb_decoder_send (decoder, data, 10);
}

void
rfb_decoder_send_key_event (RfbDecoder * decoder, unsigned int key,
    gboolean down_flag)
{
  guint8 data[8];

  data[0] = 4;
  data[1] = down_flag;
  RFB_SET_UINT16 (data + 2, 0);
  RFB_SET_UINT32 (data + 4, key);

  rfb_decoder_send (decoder, data, 8);
}

void
rfb_decoder_send_pointer_event (RfbDecoder * decoder,
    int button_mask, int x, int y)
{
  guint8 data[6];

  data[0] = 5;
  data[1] = button_mask;
  RFB_SET_UINT16 (data + 2, x);
  RFB_SET_UINT16 (data + 4, y);

  rfb_decoder_send (decoder, data, 6);
}

int
rfb_decoder_send (RfbDecoder * decoder, guint8 * buffer, int length)
{
  int ret;

  GST_DEBUG ("calling write(%d, %p, %d)", decoder->fd, buffer, length);
  ret = write (decoder->fd, buffer, length);
  if (ret < 0) {
    decoder->error_msg = g_strdup_printf ("write: %s", strerror (errno));
    return 0;
  }

  g_assert (ret == length);

  return ret;
}
