/* Backup-10 for POSIX environments
 */

/* Copyright (c) 2015 Timothe Litt litt at acm ddot org
 * All rights reserved.
 *
 * This software is provided under GPL V2, including its disclaimer of
 * warranty.  Licensing under other terms may be available from the author.
 *
 * See the LICENSE file for the well-known text of GPL V2.
 *
 * Bug reports, fixes, suggestions and improvements are welcome.
 *
 * This is a rewrite of backup.c and backwr.c, which have been
 * distributed on the internet for some time - with no authors listed.
 * This rewrite fixes many bugs, adds features, and has at most
 * a loose relationship to its predecessors.
 */

#ifndef DATA36_H
#define DATA36_H

#include <stddef.h>
#include <stdint.h>

/* 36-bit atoms, requiring only 32-bit ints */

typedef struct _WD36 {
    uint32_t lh;
    uint32_t rh;
} wd36_T;
#define BITS18 0777777

/* Set wd36 to a (32-bit) value.
 * wd36p evaluated only once. val is evaluated twice.
 */

#define SET36( wd36p, val ) do { \
        wd36_T *_p = wd36p;\
        _p->lh = ((val) >> 18) & BITS18;\
        _p->rh = (val) & BITS18; \
    } while( 0 )

/* Set wd36 to XWD lhv, rhv
 */
#define XWD36( wd36p, lhv, rhv ) do { \
        wd36_T *_p = wd36p;\
        _p->lh = (lhv) & BITS18; \
        _p->rh = (rhv) & BITS18; \
    } while( 0 )

/* Initialize a 36-bit atom to lh,,rh
 */
#define INIT36( lh, rh ) { (lh), (rh) }

/* Test atom for equal, not equal, GT, LT, GE & LE
 * These may evaluate wd36p multiple times.
 */

#define ISE36( wd36p )  ( ((wd36p)->lh | (wd36p)->rh) == 0 )
#define ISN36( wd36p )  ( ((wd36p)->lh | (wd36p)->rh) != 0 )
#define ISG36( wd36p )  ( ((wd36p)->lh & (1<<17)) == 0 )
#define ISL36( wd36p )  ( ((wd36p)->lh & (1<<17)) != 0 )
#define ISGE36( wd36p ) ( ISG36(wd36p) || ISE36(wd36p) )
#define ISLE36( wd36p ) ( ISL36(wd36p) || ISE36(wd36p) )

/* Conversions from 36-bits to byte data */

uint8_t *decode36( wd36_T *data, uint8_t *buf );
char *decodeasciz( wd36_T *data );
uint8_t *decode7ascii( wd36_T *data, uint8_t *buf );
uint8_t *decode8ascii( wd36_T *data, uint8_t *buf );

#define VERSION_BUFFER_SIZE sizeof("511BK(777777)-7")
char *decodeversion( wd36_T *data, char *buffer );

/* Conversions from byte data to 36-bit words */

uint8_t *encode36( const uint8_t *buf, wd36_T *data, size_t wds );
size_t encodeasciz( const char *string, wd36_T *data, size_t wds );
size_t encode7ascii( const char *string, wd36_T *data, size_t wds );
size_t encode8ascii( const char *string, wd36_T *data, size_t wds );

/* Conversions to and from tape packing modes */

typedef size_t (*packfn_T)(wd36_T *inbuf, size_t wc, uint8_t *outbuf, const size_t bufsize);
typedef size_t (*unpackfn_T)(uint8_t *inbuf, size_t insize, wd36_T *outbuf, size_t maxwc);

size_t unpack_core_dump(uint8_t *inbuf, size_t insize, wd36_T *outbuf, const size_t maxwc);
size_t pack_core_dump(wd36_T *inbuf, size_t wc, uint8_t *outbuf, const size_t bufsize);

/* For TAP files, 7-track is stored the right-justified in 8 bits.
 * Thus sixbit-7 and sixbit-9 happen to have the same encodings.
 */

size_t unpack_sixbit_7(uint8_t *inbuf, size_t insize, wd36_T *outbuf, size_t maxwc);
size_t pack_sixbit_7(wd36_T *inbuf, size_t wc, uint8_t *outbuf, const size_t bufsize);
#define unpack_sixbit_9 unpack_sixbit_7
#define pack_sixbit_9 pack_sixbit_7

size_t unpack_high_density(uint8_t *inbuf, size_t insize, wd36_T *outbuf, size_t maxwc);
size_t pack_high_density(wd36_T *inbuf, size_t wc, uint8_t *outbuf, const size_t bufsize);

size_t unpack_industry(uint8_t *inbuf, size_t insize, wd36_T *outbuf, size_t maxwc);
size_t pack_industry(wd36_T *inbuf, size_t wc, uint8_t *outbuf, const size_t bufsize);

size_t unpack_ansi_ascii(uint8_t *inbuf, size_t insize, wd36_T *outbuf, size_t maxwc);
size_t pack_ansi_ascii(wd36_T *inbuf, size_t wc, uint8_t *outbuf, const size_t bufsize);

#endif
