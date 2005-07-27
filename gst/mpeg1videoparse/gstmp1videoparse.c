/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstmp1videoparse.h"

GST_DEBUG_CATEGORY_STATIC (gstmpegvideoparse_debug);
#define GST_CAT_DEFAULT (gstmpegvideoparse_debug)

/* Start codes. */
#define SEQ_START_CODE 0x000001b3
#define GOP_START_CODE 0x000001b8
#define PICTURE_START_CODE 0x00000100
#define SLICE_MIN_START_CODE 0x00000101
#define SLICE_MAX_START_CODE 0x000001af
#define EXT_START_CODE 0x000001b5
#define USER_START_CODE 0x000001b2
#define SEQUENCE_ERROR_CODE 0x000001b4
#define SEQ_END_CODE 0x000001b7

static const gfloat asr_table[] = {
  0, 000,                       /* forbidden */
  1.000,                        /* square pixel */
  0.6735,
  0.7031,                       /* pal 16:9 */
  0.7615,
  0.8055,
  0.8437,                       /* ntsc 16:9 */
  0.8935,
  0.9157,                       /* pal 4:3 */
  0.9815,
  1.0255,
  1.0695,
  1.0950,                       /* ntsc 4:3 */
  1.1575,
  1.2015,
  0.0000                        /* reserved */
};
static const struct
{
  gint n, d;
} fps_table[] = {
  {
  0, 0},                        /* forbidden */
  {
  24000, 1001},                 /* ntsc film */
  {
  24, 1}, {
  25, 1},                       /* pal tv */
  {
  30000, 1001},                 /* ntsc tv */
  {
  30, 1}, {
  50, 1},                       /* pal field */
  {
  60000, 1001},                 /* ntsc field */
  {
  60, 1}, {
  15, 1},                       /* xing 15fps */
  {
  5, 1},                        /* libmpeg3 economy rate 5fps */
  {
  10, 1},                       /* libmpeg3 economy rate 10fps */
  {
  12, 1},                       /* libmpeg3 economy rate 12fps */
  {
  15, 1},                       /* libmpeg3 economy rate 15fps */
  {
  0, 0},                        /* reserved */
  {
  0, 0}                         /* reserved */
};

#define FPS(idx) ((gdouble) fps_table[idx].n / fps_table[idx].d)

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/mpeg, "
        "mpegversion = (int) { 1, 2 }, "
        "systemstream = (boolean) false, "
        "width = (int) [ 16, 4096 ], "
        "height = (int) [ 16, 4096 ], "
        "pixel_width = (int) [ 1, 255 ], "
        "pixel_height = (int) [ 1, 255 ], " "framerate = (double) [ 0, MAX ]")
    );

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/mpeg, "
        "mpegversion = (int) { 1, 2 }, " "systemstream = (boolean) false")
    );

static void gst_mp1videoparse_chain (GstPad * pad, GstData * _data);
static void gst_mp1videoparse_real_chain (Mp1VideoParse * mp1videoparse,
    GstBuffer * buf, GstPad * outpad);
static void gst_mp1videoparse_flush (Mp1VideoParse * mp1videoparse);
static GstElementStateReturn
gst_mp1videoparse_change_state (GstElement * element);

GST_BOILERPLATE (Mp1VideoParse, gst_mp1videoparse, GstElement,
    GST_TYPE_ELEMENT);

static void
gst_mp1videoparse_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstElementDetails mpeg1videoparse_details =
      GST_ELEMENT_DETAILS ("MPEG 1/2 Video Parser",
      "Codec/Parser/Video",
      "Parses and frames MPEG 1/2 video streams, provides seek",
      "Wim Taymans <wim.taymans@chello.be>, "
      "Ronald S. Bultje <rbultje@ronald.bitfreak.net>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_factory));
  gst_element_class_set_details (element_class, &mpeg1videoparse_details);
}

static void
gst_mp1videoparse_class_init (Mp1VideoParseClass * klass)
{
  GST_ELEMENT_CLASS (klass)->change_state = gst_mp1videoparse_change_state;
}

static void
gst_mp1videoparse_init (Mp1VideoParse * mp1videoparse)
{
  mp1videoparse->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get (&sink_factory),
      "sink");
  gst_element_add_pad (GST_ELEMENT (mp1videoparse), mp1videoparse->sinkpad);
  gst_pad_set_chain_function (mp1videoparse->sinkpad, gst_mp1videoparse_chain);

  mp1videoparse->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get (&src_factory),
      "src");
  gst_element_add_pad (GST_ELEMENT (mp1videoparse), mp1videoparse->srcpad);
  gst_pad_use_explicit_caps (mp1videoparse->srcpad);

  mp1videoparse->partialbuf = NULL;
  mp1videoparse->need_resync = FALSE;
  mp1videoparse->need_discont = TRUE;
  mp1videoparse->last_pts = GST_CLOCK_TIME_NONE;
  mp1videoparse->picture_in_buffer = 0;
  mp1videoparse->width = mp1videoparse->height = -1;
  mp1videoparse->ver = 0;
  mp1videoparse->width_ext = mp1videoparse->height_ext = 0;
  mp1videoparse->fps_idx = mp1videoparse->asr_idx = 0;
  mp1videoparse->fps_ext_n = mp1videoparse->fps_ext_d = 0;
  mp1videoparse->got_ext_hdr = FALSE;
  mp1videoparse->require_nego = TRUE;

  mp1videoparse->extensions = TRUE;
}

static void
mp1videoparse_parse_seq (Mp1VideoParse * mp1videoparse, guint8 * data)
{
  gint width, height, asr_idx, fps_idx;
  guint32 n = GST_READ_UINT32_BE (data);

  width = (n & 0xfff00000) >> 20;
  height = (n & 0x000fff00) >> 8;
  asr_idx = (n & 0x000000f0) >> 4;
  fps_idx = (n & 0x0000000f) >> 0;

  /* safety checks */
  if (fps_idx >= (mp1videoparse->extensions ? 13 : 9) || fps_idx <= 0)
    fps_idx = 3;
  if (asr_idx >= 15 || asr_idx <= 0)
    asr_idx = 1;
  if (asr_idx != mp1videoparse->asr_idx || fps_idx != mp1videoparse->fps_idx ||
      width != mp1videoparse->width || height != mp1videoparse->height) {
    mp1videoparse->require_nego = TRUE;
  }

  mp1videoparse->asr_idx = asr_idx;
  mp1videoparse->fps_idx = fps_idx;
  mp1videoparse->width = width;
  mp1videoparse->height = height;
}

static gboolean
gst_mp1videoparse_negotiate (Mp1VideoParse * mp1videoparse)
{
  GstCaps *caps;
  gint w, h, a_n, a_d;
  gdouble fps;

  if (!mp1videoparse->require_nego &&
      (mp1videoparse->got_ext_hdr ? 2 : 1) == mp1videoparse->ver) {
    GST_DEBUG_OBJECT (mp1videoparse, "no caps change");
    return TRUE;
  } else if (mp1videoparse->width < 0 || mp1videoparse->height < 0) {
    GST_DEBUG_OBJECT (mp1videoparse, "no headers prepared yet");
    return FALSE;
  }

  /* set-up */
  w = mp1videoparse->width;
  h = mp1videoparse->height;
  fps = (gdouble) fps_table[mp1videoparse->fps_idx].n /
      fps_table[mp1videoparse->fps_idx].d;
  a_n = asr_table[mp1videoparse->asr_idx] * 1000;
  a_d = 1000;
  if (mp1videoparse->got_ext_hdr) {
    w |= mp1videoparse->width_ext << 12;
    h |= mp1videoparse->height_ext << 12;
    fps = fps * (mp1videoparse->fps_ext_n + 1) / (mp1videoparse->fps_ext_d + 1);
  }
  caps = gst_caps_new_simple ("video/mpeg",
      "systemstream", G_TYPE_BOOLEAN, FALSE,
      "mpegversion", G_TYPE_INT, mp1videoparse->got_ext_hdr ? 2 : 1,
      "width", G_TYPE_INT, w, "height", G_TYPE_INT, h,
      "framerate", G_TYPE_DOUBLE, fps,
      "pixel-aspect-ratio", GST_TYPE_FRACTION, a_n, a_d, NULL);
  GST_DEBUG ("New mpeg1videoparse caps: %" GST_PTR_FORMAT, caps);
  mp1videoparse->ver = mp1videoparse->got_ext_hdr ? 2 : 1;
  mp1videoparse->require_nego = FALSE;

  return gst_pad_set_explicit_caps (mp1videoparse->srcpad, caps);
}

static void
mp1videoparse_parse_ext (Mp1VideoParse * mp1videoparse, guint8 * data)
{
  guint32 n1 = GST_READ_UINT32_BE (data), n2 = GST_READ_UINT32_BE (&data[3]);   /* !! */
  gint h_ext, v_ext, f_ext_n, f_ext_d;

  GST_DEBUG_OBJECT (mp1videoparse, "Received exthdr, code=0x%x/0x%x", n1, n2);
  if (((n1 & 0xf0000000) >> 28) != 0x01)
    return;

  h_ext = (n1 & 0x00018000) >> 15;
  v_ext = (n1 & 0x00006000) >> 13;
  f_ext_n = (n2 & 0x00006000) >> 13;
  f_ext_d = (n2 & 0x00001f00) >> 8;

  if (v_ext != mp1videoparse->height_ext ||
      h_ext != mp1videoparse->width_ext ||
      f_ext_d != mp1videoparse->fps_ext_d ||
      f_ext_n != mp1videoparse->fps_ext_n) {
    mp1videoparse->require_nego = TRUE;
  }

  mp1videoparse->got_ext_hdr = TRUE;
  mp1videoparse->fps_ext_n = f_ext_n;
  mp1videoparse->fps_ext_d = f_ext_d;
  mp1videoparse->width_ext = h_ext;
  mp1videoparse->height_ext = v_ext;
}

static void
mp1videoparse_read_obj (Mp1VideoParse * mp1videoparse, guint8 * data, gint size)
{
  if (size >= 4)
    switch (GST_READ_UINT32_BE (data)) {
      case SEQ_START_CODE:
        if (size >= 8) {
          mp1videoparse_parse_seq (mp1videoparse, data + 4);
        }
        break;
      case EXT_START_CODE:
        if (size >= 11) {
          mp1videoparse_parse_ext (mp1videoparse, data + 4);
        }
        break;
      default:
        break;
    }
}

static gboolean
mp1videoparse_valid_sync (Mp1VideoParse * mp1videoparse, guint32 head)
{
  return (head == SEQ_START_CODE || head == EXT_START_CODE ||
      head == GOP_START_CODE || head == PICTURE_START_CODE ||
      head == USER_START_CODE || (head >= SLICE_MIN_START_CODE &&
          head <= SLICE_MAX_START_CODE));
}

static gint
mp1videoparse_find_next_gop (Mp1VideoParse * mp1videoparse, GstBuffer * buf)
{
  guchar *data = GST_BUFFER_DATA (buf);
  gulong size = GST_BUFFER_SIZE (buf);
  gulong offset = 0;
  gint sync_zeros = 0;
  gboolean have_sync = FALSE;

  while (offset < size) {
    guchar byte = *(data + offset);

    offset++;
    if (byte == 0) {
      sync_zeros++;
    } else if (byte == 1 && sync_zeros >= 2) {
      sync_zeros = 0;
      have_sync = TRUE;
    } else if (have_sync) {
      if (byte == (SEQ_START_CODE & 0xff) ||
          byte == (GOP_START_CODE & 0xff) || byte == (EXT_START_CODE & 0xff)) {
        return offset - 4;
      } else {
        sync_zeros = 0;
        have_sync = FALSE;
      }
    } else {
      sync_zeros = 0;
    }
  }

  return -1;
}

static guint64
gst_mp1videoparse_time_code (guchar * gop, gfloat fps)
{
  guint32 data = GST_READ_UINT32_BE (gop);

  return ((((data & 0xfc000000) >> 26) * 3600 * GST_SECOND) +   /* hours */
      (((data & 0x03f00000) >> 20) * 60 * GST_SECOND) + /* minutes */
      (((data & 0x0007e000) >> 13) * GST_SECOND) +      /* seconds */
      (((data & 0x00001f80) >> 7) * GST_SECOND / fps)); /* frames */
}

static void
gst_mp1videoparse_flush (Mp1VideoParse * mp1videoparse)
{
  GST_DEBUG ("mp1videoparse: flushing");
  if (mp1videoparse->partialbuf) {
    gst_buffer_unref (mp1videoparse->partialbuf);
    mp1videoparse->partialbuf = NULL;
  }
  mp1videoparse->need_resync = TRUE;
  mp1videoparse->in_flush = TRUE;
  mp1videoparse->picture_in_buffer = 0;
}

static void
gst_mp1videoparse_chain (GstPad * pad, GstData * _data)
{
  GstBuffer *buf = GST_BUFFER (_data);
  Mp1VideoParse *mp1videoparse;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  mp1videoparse = GST_MP1VIDEOPARSE (GST_OBJECT_PARENT (pad));

  gst_mp1videoparse_real_chain (mp1videoparse, buf, mp1videoparse->srcpad);
}

static void
gst_mp1videoparse_real_chain (Mp1VideoParse * mp1videoparse, GstBuffer * buf,
    GstPad * outpad)
{
  guchar *data;
  gulong size, offset = 0;
  GstBuffer *outbuf;
  gint sync_state;
  gboolean have_sync;
  guchar sync_byte;
  guint32 head;
  gint sync_pos;
  guint64 time_stamp;
  GstBuffer *temp;

  time_stamp = GST_BUFFER_TIMESTAMP (buf);

  if (GST_IS_EVENT (buf)) {
    GstEvent *event = GST_EVENT (buf);

    switch (GST_EVENT_TYPE (event)) {
      case GST_EVENT_DISCONTINUOUS:
        mp1videoparse->need_discont = TRUE;
        /* fall-through */
      case GST_EVENT_FLUSH:
        gst_mp1videoparse_flush (mp1videoparse);
        break;
      case GST_EVENT_EOS:
        gst_mp1videoparse_flush (mp1videoparse);
        gst_event_ref (event);
        gst_pad_push (outpad, GST_DATA (event));
        gst_element_set_eos (GST_ELEMENT (mp1videoparse));
        break;
      default:
        GST_DEBUG ("Unhandled event type %d", GST_EVENT_TYPE (event));
        break;
    }

    gst_event_unref (event);
    return;
  }


  if (mp1videoparse->partialbuf) {
    GstBuffer *merge;

    offset = GST_BUFFER_SIZE (mp1videoparse->partialbuf);
    merge = gst_buffer_merge (mp1videoparse->partialbuf, buf);

    gst_buffer_unref (mp1videoparse->partialbuf);
    gst_buffer_unref (buf);

    mp1videoparse->partialbuf = merge;
  } else {
    mp1videoparse->partialbuf = buf;
    offset = 0;
  }

  data = GST_BUFFER_DATA (mp1videoparse->partialbuf);
  size = GST_BUFFER_SIZE (mp1videoparse->partialbuf);

  GST_DEBUG ("mp1videoparse: received buffer of %ld bytes %" G_GINT64_FORMAT,
      size, GST_BUFFER_TIMESTAMP (buf));

  do {
    data = GST_BUFFER_DATA (mp1videoparse->partialbuf);
    size = GST_BUFFER_SIZE (mp1videoparse->partialbuf);

    head = GST_READ_UINT32_BE (data);

    GST_DEBUG ("mp1videoparse: head is 0x%08x", (unsigned int) head);

    if (!mp1videoparse_valid_sync (mp1videoparse, head) ||
        mp1videoparse->need_resync) {
      sync_pos =
          mp1videoparse_find_next_gop (mp1videoparse,
          mp1videoparse->partialbuf);
      if (sync_pos >= 0) {
        mp1videoparse->need_resync = FALSE;
        GST_DEBUG ("mp1videoparse: found new gop at %d", sync_pos);

        if (sync_pos != 0) {
          temp =
              gst_buffer_create_sub (mp1videoparse->partialbuf, sync_pos,
              size - sync_pos);
          g_assert (temp != NULL);
          gst_buffer_unref (mp1videoparse->partialbuf);
          mp1videoparse->partialbuf = temp;
          data = GST_BUFFER_DATA (mp1videoparse->partialbuf);
          size = GST_BUFFER_SIZE (mp1videoparse->partialbuf);
          offset = 0;
        }

        head = GST_READ_UINT32_BE (data);
        /* re-call this function so that if we hadn't already, we can
         * now read the sequence header and parse video properties,
         * set caps, stream data, be happy, bla, bla, bla... */
        if (!mp1videoparse_valid_sync (mp1videoparse, head))
          g_error ("Found sync but no valid sync point at pos 0x0");
      } else {
        GST_DEBUG ("mp1videoparse: could not sync");
        gst_buffer_unref (mp1videoparse->partialbuf);
        mp1videoparse->partialbuf = NULL;
        return;
      }
    }

    if (mp1videoparse->picture_in_buffer == 1 &&
        time_stamp != GST_CLOCK_TIME_NONE) {
      mp1videoparse->last_pts = time_stamp;
    }

    sync_state = 0;
    have_sync = FALSE;

    GST_DEBUG ("mp1videoparse: searching sync");

    while (offset < size - 1) {
      sync_byte = *(data + offset);
      if (sync_byte == 0) {
        sync_state++;
      } else if ((sync_byte == 1) && (sync_state >= 2)) {
        GST_DEBUG ("mp1videoparse: code 0x000001%02x", data[offset + 1]);
        mp1videoparse_read_obj (mp1videoparse,
            &data[offset - 2], size + 2 - offset);
        if (data[offset + 1] == (PICTURE_START_CODE & 0xff)) {
          mp1videoparse->picture_in_buffer++;
          if (mp1videoparse->picture_in_buffer == 1) {
            if (time_stamp != GST_CLOCK_TIME_NONE) {
              mp1videoparse->last_pts = time_stamp;
            }
            sync_state = 0;
          } else if (mp1videoparse->picture_in_buffer == 2) {
            have_sync = TRUE;
            break;
          } else {
            GST_DEBUG ("mp1videoparse: %d in buffer",
                mp1videoparse->picture_in_buffer);
            g_assert_not_reached ();
          }
        }
        /* A new sequence (or GOP) is a valid sync too. Note that the
         * sequence header should be put in the next buffer, not here. */
        else if (data[offset + 1] == (SEQ_START_CODE & 0xFF) ||
            data[offset + 1] == (GOP_START_CODE & 0xFF) ||
            data[offset + 1] == (EXT_START_CODE & 0xff)) {
          if (mp1videoparse->picture_in_buffer == 0 &&
              data[offset + 1] == (GOP_START_CODE & 0xFF)) {
            mp1videoparse->last_pts = gst_mp1videoparse_time_code (&data[2],
                FPS (mp1videoparse->fps_idx));
          } else if (mp1videoparse->picture_in_buffer == 1) {
            have_sync = TRUE;
            break;
          } else {
            g_assert (mp1videoparse->picture_in_buffer == 0);
          }
        }
        /* end-of-sequence is a valid sync point and should be included
         * in the current picture, not the next. */
        else if (data[offset + 1] == (SEQ_END_CODE & 0xFF)) {
          if (mp1videoparse->picture_in_buffer == 1) {
            offset += 4;
            have_sync = TRUE;
            break;
          } else {
            g_assert (mp1videoparse->picture_in_buffer == 0);
          }
        } else
          sync_state = 0;
      }
      /* something else... */
      else
        sync_state = 0;

      /* go down the buffer */
      offset++;
    }

    if (have_sync) {
      offset -= 2;
      GST_DEBUG ("mp1videoparse: synced");

      outbuf = gst_buffer_create_sub (mp1videoparse->partialbuf, 0, offset);
      g_assert (outbuf != NULL);
      GST_BUFFER_TIMESTAMP (outbuf) = mp1videoparse->last_pts;
      GST_BUFFER_DURATION (outbuf) = GST_SECOND / FPS (mp1videoparse->fps_idx);
      mp1videoparse->last_pts += GST_BUFFER_DURATION (outbuf);

      if (mp1videoparse->in_flush) {
        /* FIXME, send a flush event here */
        mp1videoparse->in_flush = FALSE;
      }

      if (gst_mp1videoparse_negotiate (mp1videoparse)) {
        guint8 *p_ptr;

        if (mp1videoparse->need_discont &&
            GST_BUFFER_TIMESTAMP_IS_VALID (outbuf)) {
          GstEvent *event = gst_event_new_discontinuous (FALSE,
              GST_FORMAT_TIME, GST_BUFFER_TIMESTAMP (outbuf),
              GST_FORMAT_UNDEFINED);

          GST_DEBUG ("prepending discont event");
          gst_pad_push (outpad, GST_DATA (event));
          mp1videoparse->need_discont = FALSE;
        }

        /* get keyframe flag */
        p_ptr = GST_BUFFER_DATA (outbuf);
        while (p_ptr[0] != 0x0 || p_ptr[1] != 0x0 ||
            p_ptr[2] != 0x1 || p_ptr[3] != (PICTURE_START_CODE & 0xff))
          p_ptr++;
        if (((p_ptr[5] >> 3) & 0x7) == 0x1) {
          GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_KEY_UNIT);
        }

        /* push */
        GST_DEBUG ("mp1videoparse: pushing %d bytes at %" GST_TIME_FORMAT
            ", key=%s", GST_BUFFER_SIZE (outbuf),
            GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (outbuf)),
            GST_BUFFER_FLAG_IS_SET (outbuf,
                GST_BUFFER_KEY_UNIT) ? "yes" : "no");
        gst_pad_push (outpad, GST_DATA (outbuf));
      } else {
        GST_DEBUG ("No capsnego yet, delaying buffer push");
        gst_buffer_unref (outbuf);
      }
      mp1videoparse->picture_in_buffer = 0;

      if (size != offset) {
        temp =
            gst_buffer_create_sub (mp1videoparse->partialbuf, offset,
            size - offset);
      } else {
        temp = NULL;
      }
      gst_buffer_unref (mp1videoparse->partialbuf);
      mp1videoparse->partialbuf = temp;
      offset = 0;
    } else {
      if (time_stamp != GST_CLOCK_TIME_NONE)
        mp1videoparse->last_pts = time_stamp;
      return;
    }
  } while (mp1videoparse->partialbuf != NULL);
}

static GstElementStateReturn
gst_mp1videoparse_change_state (GstElement * element)
{
  Mp1VideoParse *mp1videoparse;

  g_return_val_if_fail (GST_IS_MP1VIDEOPARSE (element), GST_STATE_FAILURE);

  mp1videoparse = GST_MP1VIDEOPARSE (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_PAUSED_TO_READY:
      gst_mp1videoparse_flush (mp1videoparse);
      mp1videoparse->need_discont = TRUE;
      mp1videoparse->width = mp1videoparse->height = -1;
      mp1videoparse->fps_idx = 3;
      mp1videoparse->asr_idx = 1;
      break;
    default:
      break;
  }

  return GST_CALL_PARENT_WITH_DEFAULT (GST_ELEMENT_CLASS, change_state,
      (element), GST_STATE_SUCCESS);
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gstmpegvideoparse_debug, "mpegvideoparse", 0,
      "MPEG 1/2 video parsing element");

  return gst_element_register (plugin, "mpeg1videoparse",
      GST_RANK_NONE, GST_TYPE_MP1VIDEOPARSE);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "mpeg1videoparse",
    "MPEG-1 video parser",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE, GST_ORIGIN)
