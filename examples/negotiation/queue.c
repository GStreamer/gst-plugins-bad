/*
 * queue.c
 *
 * demo application for negotiation over queues. 
 */

#include <string.h>
#include <gtk/gtk.h>
#include <gst/gst.h>

static GstElement *pipeline;
static GstElement *src;
static GstElement *queue;
static GstElement *sink;

static GstPad *pad1, *peer1;
static GstPad *pad2, *peer2;

static gboolean caught_error = FALSE;
static GMainLoop *loop;

static gboolean
message_received (GstBus * bus, GstMessage * message, gpointer data)
{
  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_EOS:
      if (g_main_loop_is_running (loop))
        g_main_loop_quit (loop);
      break;
    case GST_MESSAGE_ERROR:
      gst_object_default_error (GST_MESSAGE_SRC (message),
          GST_MESSAGE_ERROR_GERROR (message),
          GST_MESSAGE_ERROR_DEBUG (message));
      caught_error = TRUE;
      if (g_main_loop_is_running (loop))
        g_main_loop_quit (loop);
      break;
    default:
      break;
  }
  gst_message_unref (message);

  return TRUE;
}

static void
block_done (GstPad * pad, gboolean blocked, gpointer data)
{
  if (blocked) {
    g_print ("pad blocked\n");
    /* let's unlink to be cool too */
    gst_pad_unlink (pad2, peer2);
  } else {
    g_print ("pad unblocked\n");
  }
}

static gboolean
do_block (GstPipeline * pipeline)
{
  static gint iter = 0;


  if (iter++ % 2) {
    g_print ("blocking pad..");
    if (!gst_pad_set_blocked_async (pad2, TRUE, block_done, NULL))
      g_print ("was blocked\n");
  } else {
    /* and relink */
    gst_pad_link (pad2, peer2);
    g_print ("unblocking pad..");
    if (!gst_pad_set_blocked_async (pad2, FALSE, block_done, NULL))
      g_print ("was unblocked\n");
  }
  return TRUE;
}

static gboolean
do_renegotiate (GstPipeline * pipeline)
{
  GstCaps *caps;
  static gint iter = 0;

  g_print ("reneg\n");

  if (iter++ % 2) {
    caps = gst_caps_new_simple ("video/x-raw-yuv",
        "format", GST_TYPE_FOURCC, GST_STR_FOURCC ("I420"),
        "width", G_TYPE_INT, 320,
        "height", G_TYPE_INT, 240, "framerate", G_TYPE_DOUBLE, 5.0, NULL);
  } else {
    caps = gst_caps_new_simple ("video/x-raw-yuv",
        "format", GST_TYPE_FOURCC, GST_STR_FOURCC ("YUY2"),
        "width", G_TYPE_INT, 240,
        "height", G_TYPE_INT, 120, "framerate", G_TYPE_DOUBLE, 30.0, NULL);
  }

  gst_pad_relink_filtered (pad1, peer1, caps);
  gst_caps_unref (caps);

  return TRUE;
}

int
main (int argc, char *argv[])
{
  GstBus *bus;

  gtk_init (&argc, &argv);
  gst_init (&argc, &argv);

  pipeline = gst_pipeline_new ("pipeline");
  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  loop = g_main_loop_new (NULL, FALSE);
  gst_bus_add_watch (bus, message_received, pipeline);

  src = gst_element_factory_make ("videotestsrc", "src");
  queue = gst_element_factory_make ("queue", "queue");
  sink = gst_element_factory_make ("xvimagesink", "sink");

  gst_bin_add (GST_BIN (pipeline), src);
  gst_bin_add (GST_BIN (pipeline), queue);
  gst_bin_add (GST_BIN (pipeline), sink);

  pad1 = gst_element_get_pad (src, "src");
  peer1 = gst_element_get_pad (queue, "sink");

  pad2 = gst_element_get_pad (queue, "src");
  peer2 = gst_element_get_pad (sink, "sink");

  gst_pad_link (pad1, peer1);
  gst_pad_link (pad2, peer2);

  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  g_timeout_add (1000, (GSourceFunc) do_block, pipeline);

  g_main_loop_run (loop);

  g_timeout_add (200, (GSourceFunc) do_renegotiate, pipeline);

  gst_element_set_state (pipeline, GST_STATE_NULL);

  gst_object_unref (GST_OBJECT (pipeline));

  return 0;
}
