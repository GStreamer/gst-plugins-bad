#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <gst/gst.h>
#include <gtk/gtk.h>

extern gboolean _gst_plugin_spew;

gboolean idle_func (gpointer data);

GtkWidget *drawingarea;

void
spectrum_chain (GstElement * sink, GstBuffer * buf, GstPad * pad,
    gpointer unused)
{
  gint i;
  guchar *data = GST_BUFFER_DATA (buf);
  GdkRectangle rect = { 0, 0, GST_BUFFER_SIZE (buf), 32 };

  gdk_window_begin_paint_rect (drawingarea->window, &rect);
  gdk_draw_rectangle (drawingarea->window, drawingarea->style->black_gc,
      TRUE, 0, 0, GST_BUFFER_SIZE (buf), 32);
  for (i = 0; i < GST_BUFFER_SIZE (buf); i++) {
    gdk_draw_rectangle (drawingarea->window, drawingarea->style->white_gc,
        TRUE, i, 32 - data[i], 1, data[i]);
  }
  gdk_window_end_paint (drawingarea->window);
}

int
main (int argc, char *argv[])
{
  GstElement *bin;
  GstElement *src, *spectrum, *sink;
  GstCaps *filtercaps;
  GtkWidget *appwindow;

  gst_init (&argc, &argv);
  gtk_init (&argc, &argv);

  bin = gst_pipeline_new ("bin");

  src = gst_element_factory_make (DEFAULT_AUDIOSRC, "src");
  g_object_set (G_OBJECT (src), "buffersize", (gulong) 1024 * sizeof (gint16),
      NULL);
  spectrum = gst_element_factory_make ("spectrum", "spectrum");
  g_object_set (G_OBJECT (spectrum), "width", 256, NULL);
  sink = gst_element_factory_make ("fakesink", "sink");
  g_object_set (G_OBJECT (sink), "signal-handoffs", TRUE, NULL);
  g_signal_connect (sink, "handoff", G_CALLBACK (spectrum_chain), NULL);

  gst_bin_add_many (GST_BIN (bin), src, spectrum, sink, NULL);

  filtercaps =
      gst_caps_new_simple ("audio/x-raw-int", "rate", G_TYPE_INT, 11025, NULL);
  if (!gst_element_link_filtered (src, spectrum, filtercaps))
    g_error ("Linking source to spectrum failed\n");
  gst_caps_free (filtercaps);
  if (!gst_element_link (spectrum, sink))
    g_error ("Linking spectrum to sink failed\n");

  appwindow = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  g_signal_connect (appwindow, "delete-event", G_CALLBACK (gtk_main_quit),
      NULL);
  drawingarea = gtk_drawing_area_new ();
  gtk_drawing_area_size (GTK_DRAWING_AREA (drawingarea), 256, 32);
  gtk_container_add (GTK_CONTAINER (appwindow), drawingarea);
  gtk_widget_show_all (appwindow);

  gst_element_set_state (GST_ELEMENT (bin), GST_STATE_PLAYING);

  g_idle_add (idle_func, bin);

  gtk_main ();

  return 0;
}


gboolean
idle_func (gpointer data)
{
  return gst_bin_iterate (data);
}
