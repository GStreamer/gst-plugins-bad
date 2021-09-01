#include "amftracewriter.hpp"
#include <iostream>
#include <iomanip>
#include <gst/gst.h>

GST_DEBUG_CATEGORY_STATIC(gst_amfh264enc_debug_category);
#define GST_CAT_DEFAULT gst_amfh264enc_debug_category

void GstAMFTraceWriter::Write(const wchar_t *scope, const wchar_t *message)
{
	GST_INFO("[AMF] [%ls] %ls", scope, message);
}
