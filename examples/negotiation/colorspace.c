/*
 * colorspace.c
 *
 * demo application for negotiation of a simple plugin.
 */

#include <string.h>
#include <gst/gst.h>

static GstElement *pipeline;
static GstElement *space;

static GstPad *src;
static GstPad *sink;
static GstPad *test;

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

static GstFlowReturn
my_chain (GstPad * pad, GstBuffer * buffer)
{
  g_print ("got buffer\n");
  return GST_FLOW_OK;
}

int
main (int argc, char *argv[])
{
  GstBus *bus;
  GstBuffer *buffer;

  gst_init (&argc, &argv);

  pipeline = gst_pipeline_new ("pipeline");
  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  loop = g_main_loop_new (NULL, FALSE);
  gst_bus_add_watch (bus, message_received, pipeline);

  space = gst_element_factory_make ("ffmpegcolorspace", "space");

  gst_bin_add (GST_BIN (pipeline), space);

  sink = gst_element_get_pad (space, "sink");
  src = gst_element_get_pad (space, "src");
  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  test = gst_pad_new ("test", GST_PAD_SINK);
  gst_pad_set_chain_function (test, my_chain);

  gst_pad_link (src, test);
  gst_pad_set_active (test, TRUE);

  gst_pad_set_caps (sink,
      gst_caps_new_simple ("video/x-raw-yuv",
          "format", GST_TYPE_FOURCC, GST_STR_FOURCC ("YUY2"),
          "width", G_TYPE_INT, 240,
          "height", G_TYPE_INT, 120, "framerate", G_TYPE_DOUBLE, 30.0, NULL));

  while (g_main_context_iteration (NULL, FALSE));

  buffer = gst_buffer_new ();

  GST_REAL_PAD (sink)->chainfunc (sink, buffer);

  while (g_main_context_iteration (NULL, FALSE));

  gst_element_set_state (pipeline, GST_STATE_NULL);

  gst_object_unref (GST_OBJECT (pipeline));

  return 0;
}
