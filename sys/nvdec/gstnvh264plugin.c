/*  Copyright (C) 2015-2016 Setplex. All right reserved.
    This file is part of Rixjob.
    Rixjob is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
    Rixjob is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
    You should have received a copy of the GNU General Public License
    along with Rixjob.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gst/gst.h>

#include "gstnvh264dec.h"

static gboolean
plugin_init (GstPlugin * plugin)
{
  gst_element_register (plugin, "nvh264dec", GST_RANK_PRIMARY + 3,
      GST_TYPE_NVH264DEC);
  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    nvh264dec,
    "Nvidia cuda decoder plugin",
    plugin_init,
    VERSION, "BSD", "Setplex GStreamer plugins", "http://www.setplex.com")
