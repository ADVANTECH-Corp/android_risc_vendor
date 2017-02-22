/*
 * Copyright 2013, Telit Communications S.p.a.
 *
 * This file is licensed under a commercial license agreement.
 */

#ifndef ANDROID_RIL_AT_TIMEOUT_H
#define ANDROID_RIL_AT_TIMEOUT_H 1

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __cplusplus
}
#endif

#define AT_DELAY_ISSUING                ( 80 * 1000 )

#define MSEC                            1000

#define AT_TIMEOUT_NORMAL               ( 10 * MSEC )
#define AT_TIMEOUT_CCWA                 ( 16 * MSEC )
#define AT_TIMEOUT_CFUN                 ( 15 * MSEC )
#define AT_TIMEOUT_CPBS                 ( 6 * MSEC )
#define AT_TIMEOUT_CPBR                 ( 16 * MSEC )
#define AT_TIMEOUT_COPS                 ( 121 * MSEC )
#define AT_TIMEOUT_CSCA                 ( 6 * MSEC )
#define AT_TIMEOUT_CHLD                 ( 31 * MSEC )
#define AT_TIMEOUT_CLIP                 ( 16 * MSEC )
#define AT_TIMEOUT_CPWD                 ( 16 * MSEC )
#define AT_TIMEOUT_CPIN                 ( 6 * MSEC )
#define AT_TIMEOUT_CLCK                 ( 26 * MSEC )
#define AT_TIMEOUT_CCFC                 ( 16 * MSEC )
#define AT_TIMEOUT_CLIR                 ( 16 * MSEC )
#define AT_TIMEOUT_CGATT                ( 11 * MSEC )
#define AT_TIMEOUT_D                    ( 31 * MSEC )
#define AT_TIMEOUT_H                    ( 31 * MSEC )
#define AT_TIMEOUT_A                    ( 31 * MSEC )
#define AT_TIMEOUT_CRSM                 ( 16 * MSEC )
#define AT_TIMEOUT_CMGD                 ( 26 * MSEC )
#define AT_TIMEOUT_CGACT                ( 151 * MSEC )
#define AT_TIMEOUT_CMGW                 ( 8 * MSEC )
#define AT_TIMEOUT_CMGS                 ( 62 * MSEC )
#define AT_TIMEOUT_SERVICE              ( 15 * MSEC )
#define AT_TIMEOUT_ECM                  ( 90 * MSEC )

#endif /*ANDROID_RIL_AT_TIMEOUT_H */
