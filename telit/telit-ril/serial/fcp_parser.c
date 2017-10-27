/*
 * Copyright 2006, The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Based on reference-ril by The Android Open Source Project.
 *
 * Heavily modified for ST-Ericsson U300 modems.
 * Author: Dmitry Tarnyagin <dmitry.tarnyagin@stericsson.com>
 * Modify: Wenjun Shi <b42754@freescale.com>
 * Modified for Innocomm Amazon1901 and Huawei EM770W  modems.
 */
/* Copyright (C) 2013 Freescale Semiconductor, Inc. */

#include <memory.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <cutils/properties.h>

#define LOG_TAG "RILV"
#include <utils/Log.h>

#include "serial/fcp_parser.h"
#include "misc.h"

char char2nib(char c)
{
    if (c >= 0x30 && c <= 0x39)
        return c - 0x30;

    if (c >= 0x41 && c <= 0x46)
        return c - 0x41 + 0xA;

    if (c >= 0x61 && c <= 0x66)
        return c - 0x61 + 0xA;

    return 0;
}

int parseTlv(/*in*/ const char *stream,
        /*in*/ const char *end,
        /*out*/ struct tlv *tlv)
{
#define TLV_STREAM_GET(stream, end, p) \
do { \
if (stream + 1 >= end){ \
/*
 * some 3g modem's response is incomplete, e.g. Huawei EM770W, so don't do size
 * checking for compatibility.
 */ \
/*goto underflow;*/ \
} \
p = ((unsigned)char2nib(stream[0]) << 4) \
| ((unsigned)char2nib(stream[1]) << 0); \
stream += 2; \
} while (0)

    size_t size;

    TLV_STREAM_GET(stream, end, tlv->tag);
    TLV_STREAM_GET(stream, end, size);
    if (stream + size * 2 > end){
       RLOGW("parseTlv: response size is incomplete");
       /*
        * some 3g modem's response is incomplete, e.g. Huawei EM770W, so don't do size
        * checking for compatibility.
        */
       // goto underflow;
       }
    tlv->data = &stream[0];
    tlv->end = &stream[size * 2];
    return 0;

underflow:
    return -EINVAL;
#undef TLV_STREAM_GET
}

int binaryToString(/*in*/ const unsigned char *binary,
        /*in*/ size_t len,
        /*out*/ char *string)
{
    int pos;
    const unsigned char *it;
    const unsigned char *end = &binary[len];
    static const char nibbles[] =
        {'0', '1', '2', '3', '4', '5', '6', '7',
         '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};

    if (end < binary)
        return -EINVAL;

    for (pos = 0, it = binary; it != end; ++it, pos += 2) {
        string[pos + 0] = nibbles[*it >> 4];
        string[pos + 1] = nibbles[*it & 0x0f];
    }
    string[pos] = 0;
    return 0;
}

int fcp_to_ts_51011(/*in*/ const char *stream,
        /*in*/ size_t len,
        /*out*/ struct ts_51011_921_resp *out)
{
    const char *end = &stream[len];
    struct tlv fcp;
    int ret = parseTlv(stream, end, &fcp);
    const char *what = NULL;
#define FCP_CVT_THROW(_ret, _what) \
do { \
ret = _ret; \
what = _what; \
goto except; \
} while (0)

    if (ret < 0)
        FCP_CVT_THROW(ret,
                "ETSI TS 102 221, 11.1.1.3: FCP template TLV structure");

    if (fcp.tag != 0x62){
        RLOGE("fcp_to_ts_51011: tag: 0x%x", fcp.tag);
        FCP_CVT_THROW(-EINVAL,
                "ETSI TS 102 221, 11.1.1.3: FCP template tag");
    }

    /*
     * NOTE: Following fields do not exist in FCP template:
     * - file_acc
     * - file_status
     */

    memset(out, 0, sizeof(*out));
    while (fcp.data < fcp.end) {
        unsigned char fdbyte;
        size_t property_size;
        struct tlv tlv;
        ret = parseTlv(fcp.data, end, &tlv);
        if (ret < 0)
            FCP_CVT_THROW(ret, "FCP property TLV structure");
        property_size = (tlv.end - tlv.data) / 2;

        switch (tlv.tag) {
            case 0x80: /* File size, ETSI TS 102 221, 11.1.1.4.1 */
                /* File size > 0xFFFF is not supported by ts_51011 */
                if (property_size != 2)
                    FCP_CVT_THROW(-ENOTSUP, "Unsupported file size");
                /* be16 on both sides */
                ((char*)&out->file_size)[0] = TLV_DATA(tlv, 0);
                ((char*)&out->file_size)[1] = TLV_DATA(tlv, 1);
            break;
            case 0x83: /* File identifier, ETSI TS 102 221, 11.1.1.4.4 */
                /* Sanity check */
                if (property_size != 2)
                    FCP_CVT_THROW(-EINVAL, "Invalid file id");
                /* be16 on both sides */
                ((char*)&out->file_id)[0] = TLV_DATA(tlv, 0);
                ((char*)&out->file_id)[1] = TLV_DATA(tlv, 1);
            break;
            case 0x82: /* File descriptior, ETSI TS 102 221, 11.1.1.4.3 */
                /* Sanity check */
                if (property_size < 2)
                    FCP_CVT_THROW(-EINVAL, "Invalid file descr");
                fdbyte = TLV_DATA(tlv, 0);
                /* ETSI TS 102 221, Table 11.5 for FCP fields */
                /* 3GPP TS 51 011, 9.2.1 and 9.3 for 'out' fields */
                if ((fdbyte & 0xBF) == 0x38) {
                    out->file_type = 2; /* DF of ADF */
                } else if ((fdbyte & 0xB0) == 0x00) {
                    out->file_type = 4; /* EF */
                    out->file_status = 1; /* Not invalidated */
                    ++out->data_size; /* file_structure field is valid */
                    if ((fdbyte & 0x07) == 0x01) {
                        out->file_structure = 0; /* Transparent */
                    } else {
                        if (property_size < 5)
                            FCP_CVT_THROW(-EINVAL,
                                    "Invalid non-transparent file descriptor");
                        ++out->data_size; /* record_size field is valid */
                        out->record_size = TLV_DATA(tlv, 3);
                        if ((fdbyte & 0x07) == 0x06) {
                            out->file_structure = 3; /* Cyclic */
                        } else if ((fdbyte & 0x07) == 0x02) {
                            out->file_structure = 1; /* Linear fixed */
                        } else {
                            FCP_CVT_THROW(-EINVAL, "Invalid file structure");
                        }
                    }
                } else {
                    out->file_type = 0; /* RFU */
                }
                break;
        }
        fcp.data = tlv.end;
    }

finally:
    return ret;

except:
#undef FCP_CVT_THROW
    RLOGE("FCP to TS 510 11: Specification violation: %s.", what);
    goto finally;
}
