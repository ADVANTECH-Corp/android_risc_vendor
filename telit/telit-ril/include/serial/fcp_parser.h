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

#ifndef FCP_PARSER_H
#define FCP_PARSER_H

#include <stdint.h>
#include <endian.h>

#define TLV_DATA(tlv, pos) (((unsigned)char2nib(tlv.data[(pos) * 2 + 0]) << 4) | \
((unsigned)char2nib(tlv.data[(pos) * 2 + 1]) << 0))

struct tlv {
    unsigned tag;
    const char *data;
    const char *end;
};

struct ts_51011_921_resp {
    uint8_t rfu_1[2];
    uint16_t file_size; /* be16 */
    uint16_t file_id; /* be16 */
    uint8_t file_type;
    uint8_t rfu_2;
    uint8_t file_acc[3];
    uint8_t file_status;
    uint8_t data_size;
    uint8_t file_structure;
    uint8_t record_size;
} __attribute__((packed));

int parseTlv(const char *stream,
        const char *end,
        struct tlv *tlv);

int binaryToString(const unsigned char *binary,
        size_t len,
        char *string);

int fcp_to_ts_51011(const char *stream,
        size_t len,
        struct ts_51011_921_resp *out);

#endif
