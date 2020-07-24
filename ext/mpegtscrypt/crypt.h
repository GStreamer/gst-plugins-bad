/* GStreamer MPEGTCCRYPT (MPEG TS Encrypt/Decrypt) plugin
 *
 * Copyright (C) 2020 Karim Davoodi <karimdavoodi@gmail.com>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */
#ifndef __CONFIG_H__
#define __CONFIG_H__

#include <gst/gst.h>
#include <dvbcsa/dvbcsa.h>
#include <openssl/aes.h>
#include "gstmpegtscrypt.h"

#define CODEWORD_LENGTH 16
#define BISSKEY_LENGTH 6

void crypt_init(GstMpegtsCrypt* filter);
void crypt_finish(GstMpegtsCrypt* filter);
        
void crypt_packet_aes(GstMpegtsCrypt* filter, unsigned char *ts_packet);
void crypt_packet_biss(GstMpegtsCrypt* filter, unsigned char *ts_packet);
#endif
