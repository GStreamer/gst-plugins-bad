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
#include <gst/gst.h>
#include "gstmpegtscrypt.h"
#include "crypt.h"

int _decode_hex_char (char c);
int _decode_hex_string (const char *hex, unsigned char *bin, int asc_len);
gboolean _init_biss_key (GstMpegtsCrypt*filter,  char* key_str);
unsigned char ts_packet_get_payload_offset (unsigned char *ts_packet);

int _decode_hex_char (char c) {
    if ( (c >= '0') && (c <= '9')) return c - '0';
    if ( (c >= 'A') && (c <= 'F')) return c - 'A' + 10;
    if ( (c >= 'a') && (c <= 'f')) return c - 'a' + 10;
    return -1;
}
int _decode_hex_string (const char *hex, unsigned char *bin, int asc_len) {
    int i;
    for (i = 0; i < asc_len; i += 2) {
        int n1 = _decode_hex_char (hex[i + 0]);
        int n2 = _decode_hex_char (hex[i + 1]);
        if (n1 == -1 || n2 == -1)
            return -1;
        bin[i / 2] = (n1 << 4) | (n2 & 0xf);
    }
    return asc_len / 2;
}
gboolean _init_biss_key (GstMpegtsCrypt*filter,  char* key_str)
{
    char * str = key_str;
    unsigned char key[16];
 
    int key_len = strlen(key_str);

    if (key_len > 2 && key_str[0] == '0' && key_str[1] == 'x'){
        str = str + 2; 
        key_len -= 2;
    }
    // Sometimes the BISS keys are entered with their checksums already calculated
    // (16 symbols, 8 bytes)
    // This is the same as constant cw with the same key for even and odd
    if (key_len == (BISSKEY_LENGTH + 2) * 2) {
        if (_decode_hex_string (str, key, key_len) < 0) {
            GST_ERROR_OBJECT (filter, "Invalid hex string for BISS key.");
            return FALSE;
        }
    } else {
        // BISS key without checksum (12 symbols, 6 bytes)
        if (key_len != BISSKEY_LENGTH * 2) {
            GST_ERROR_OBJECT (filter, "Invalid BISS key len. must be %d or %d", 
                    BISSKEY_LENGTH*2,
                   (BISSKEY_LENGTH+2)*2);
            return FALSE;
        }
        if (_decode_hex_string (str, key, key_len) < 0) {
            GST_ERROR_OBJECT (filter, "Invalid hex string for BISS key");
            return FALSE;
        }
        // Calculate BISS KEY crc
        memmove (key + 4, key + 3, 3);
        key[3] = (unsigned char) (key[0] + key[1] + key[2]);
        key[7] = (unsigned char) (key[4] + key[5] + key[6]);
    }
    // Even and odd keys are the same
    dvbcsa_key_set (key, filter->biss_csakey[0]);
    dvbcsa_key_set (key, filter->biss_csakey[1]);
    GST_DEBUG_OBJECT (filter, "Init BISS key");
    return TRUE;
}
// copy from libtsfuncs
unsigned char ts_packet_get_payload_offset (unsigned char *ts_packet) {

    unsigned char adapt_field   = (ts_packet[3] &~ 0xDF) >> 5; // 11x11111
    unsigned char payload_field = (ts_packet[3] &~ 0xEF) >> 4; // 111x1111

    if (ts_packet[0] != 0x47)
        return 0;

    if (!adapt_field && !payload_field) 
        return 0;

    if (adapt_field) {
        unsigned char adapt_len = ts_packet[4];
        if (payload_field && adapt_len > 182) // Validity checks
            return 0;
        if (!payload_field && adapt_len > 183)
            return 0;
        if (adapt_len + 4 > 188) // adaptation field takes the whole packet
            return 0;
        return 4 + 1 + adapt_len; // ts header + adapt_field_len_byte + adapt_field_len
    } else {
        return 4; // No adaptation, data starts directly after TS header
    }
}
void crypt_packet_aes (GstMpegtsCrypt* filter, unsigned char *ts_packet) {

    unsigned char *in;
    unsigned char *out;
    unsigned int payload_offset = ts_packet_get_payload_offset (ts_packet);
    // TODO: the last remaind of AES_BLOCK_SIZE bytes not crypt
    for (int i = payload_offset; i < 188-AES_BLOCK_SIZE; i += AES_BLOCK_SIZE){
       in  = ts_packet + i;
       out = ts_packet + i;
        switch (filter->method){
            case MPEGTSCRYPT_METHOD_AES128_CBC:
                if (filter->operation == MPEGTSCRYPT_OPERATION_ENC)
                    AES_cbc_encrypt (in , out, AES_BLOCK_SIZE, & (filter->aes_enc_key), 
                            filter->aes_iv_enc, AES_ENCRYPT);
                else
                    AES_cbc_encrypt (in , out, AES_BLOCK_SIZE, & (filter->aes_dec_key), 
                            filter->aes_iv_dec, AES_DECRYPT);
                break;
            case MPEGTSCRYPT_METHOD_AES128_ECB:
                if (filter->operation == MPEGTSCRYPT_OPERATION_ENC)
                    AES_ecb_encrypt (in , out, & (filter->aes_enc_key), AES_ENCRYPT);
                else
                    AES_ecb_encrypt (in , out, & (filter->aes_dec_key), AES_DECRYPT);
                break;
            case MPEGTSCRYPT_METHOD_AES256_CBC:
                if (filter->operation == MPEGTSCRYPT_OPERATION_ENC)
                    AES_cbc_encrypt (in , out, AES_BLOCK_SIZE, & (filter->aes_enc_key), 
                            filter->aes_iv_enc, AES_ENCRYPT);
                else
                    AES_cbc_encrypt (in , out, AES_BLOCK_SIZE, & (filter->aes_dec_key), 
                            filter->aes_iv_dec, AES_DECRYPT);
                break;
            case MPEGTSCRYPT_METHOD_AES256_ECB:
                if (filter->operation == MPEGTSCRYPT_OPERATION_ENC)
                    AES_ecb_encrypt (in , out, & (filter->aes_enc_key), AES_ENCRYPT);
                else
                    AES_ecb_encrypt (in , out, & (filter->aes_dec_key), AES_DECRYPT);
                break;
            default:
                break;
        }
    }
}
void crypt_packet_biss (GstMpegtsCrypt* filter, unsigned char *ts_packet) {
    static gboolean key_idx = 0;

    unsigned int payload_offset = ts_packet_get_payload_offset (ts_packet);
    GST_LOG_OBJECT (filter, "biss key idx: %d pyload size: %d",key_idx, 188 - payload_offset);

    if (filter->operation == MPEGTSCRYPT_OPERATION_ENC){
        if (key_idx == 0)  ts_packet[3] |= 2 << 6;   // even key
        else              ts_packet[3] |= 3 << 6;   // odd key
        dvbcsa_encrypt (filter->biss_csakey[key_idx], ts_packet + payload_offset, 
                188 - payload_offset);
        key_idx = key_idx == 0 ? 1 : 0;
    }else{
        int scramble_idx =  ts_packet[3] >> 6;
        if (scramble_idx > 1) {
            unsigned int key_idx = scramble_idx - 2;
            ts_packet[3] = ts_packet[3] &~ 0xc0; // set not scrambled (11xxxxxx)
            dvbcsa_decrypt (filter->biss_csakey[key_idx], ts_packet + payload_offset, 
                    188 - payload_offset);
        }else GST_WARNING_OBJECT (filter, "Ts packet is not scrambled");
    }
}
void crypt_finish (GstMpegtsCrypt* filter)
{
    GST_DEBUG_OBJECT (filter, "Finish crypto");
    switch (filter->method){
        case MPEGTSCRYPT_METHOD_BISS: 
            dvbcsa_key_free (filter->biss_csakey[0]);
            dvbcsa_key_free (filter->biss_csakey[1]);
            break;
        default:
            break;
    }

}
void crypt_init (GstMpegtsCrypt* filter)
{
    int aes_bit = 256;
    GST_DEBUG_OBJECT (filter, "Init crypto by key '%s' ", filter->key);
    switch (filter->method){
        case MPEGTSCRYPT_METHOD_BISS: 
            filter->biss_csakey[0] = dvbcsa_key_alloc ();
            filter->biss_csakey[1] = dvbcsa_key_alloc ();
            _init_biss_key (filter, filter->key );
            break;

        case MPEGTSCRYPT_METHOD_AES128_ECB:
        case MPEGTSCRYPT_METHOD_AES128_CBC:
            aes_bit = 128;
        case MPEGTSCRYPT_METHOD_AES256_ECB:
        case MPEGTSCRYPT_METHOD_AES256_CBC:
            AES_set_encrypt_key ( (const unsigned char*)filter->key, aes_bit, 
                    & (filter->aes_enc_key));
            AES_set_decrypt_key ( (const unsigned char*)filter->key, aes_bit, 
                    & (filter->aes_dec_key));
            memset (filter->aes_iv_dec, 0xf1, AES_BLOCK_SIZE);
            memset (filter->aes_iv_enc, 0xf1, AES_BLOCK_SIZE);
            break;
    }

}
