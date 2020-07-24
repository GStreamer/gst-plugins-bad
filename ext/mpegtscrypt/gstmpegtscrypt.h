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
#ifndef __GST_MPEGTSCRYPT_H__
#define __GST_MPEGTSCRYPT_H__

#include <dvbcsa/dvbcsa.h>
#include <openssl/aes.h>
#include <gst/gst.h>
#include <gst/base/base.h>

#define TS_PACKET_SIZE 188


G_BEGIN_DECLS

/* #defines don't like whitespacey bits */
#define GST_TYPE_MPEGTSCRYPT \
  (gst_mpegts_crypt_get_type())
#define GST_MPEGTSCRYPT(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_MPEGTSCRYPT,GstMpegtsCrypt))
#define GST_MPEGTSCRYPT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_MPEGTSCRYPT,GstMpegtsCryptClass))
#define GST_IS_MPEGTSCRYPT(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_MPEGTSCRYPT))
#define GST_IS_MPEGTSCRYPT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_MPEGTSCRYPT))

typedef struct _GstMpegtsCrypt      GstMpegtsCrypt;
typedef struct _GstMpegtsCryptClass GstMpegtsCryptClass;


enum
{
  PROP_0,
  PROP_METHOD,
  PROP_KEY,
  PROP_OPERATION
};
typedef enum {
    MPEGTSCRYPT_METHOD_BISS,
    MPEGTSCRYPT_METHOD_AES128_ECB,
    MPEGTSCRYPT_METHOD_AES128_CBC,
    MPEGTSCRYPT_METHOD_AES256_ECB,
    MPEGTSCRYPT_METHOD_AES256_CBC
} GstMpegTsCryptMethod;

typedef enum {
    MPEGTSCRYPT_OPERATION_DEC,
    MPEGTSCRYPT_OPERATION_ENC
} GstMpegTsCryptOperation;

struct _GstMpegtsCrypt
{
  GstElement element;

  GstPad *sinkpad, *srcpad;
  GstAdapter* adapter;

  GstMpegTsCryptMethod method;
  GstMpegTsCryptOperation operation;

  gchar key[256];
  
  // BISS key
  dvbcsa_key_t	*biss_csakey[2];
  // AES  key
  AES_KEY   aes_enc_key;
  AES_KEY   aes_dec_key;
  unsigned char aes_iv_enc[AES_BLOCK_SIZE];
  unsigned char aes_iv_dec[AES_BLOCK_SIZE];

};

struct _GstMpegtsCryptClass 
{
  GstElementClass parent_class;
};

GType gst_mpegts_crypt_get_type (void);

G_END_DECLS
#endif /* __GST_MPEGTSCRYPT_H__ */
