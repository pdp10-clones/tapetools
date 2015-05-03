/* Backup-10 for POSIX environments
 */

/* Copyright (c) 2015 Timothe Litt litt at acm ddot org
 * All rights reserved.
 *
 * This software is provided under GPL V2, including its disclaimer of
 * warranty.  Licensing under other terms may be available from the author.
 *
 * See the COPYING file for the well-known text of GPL V2.
 *
 * Bug reports, fixes, suggestions and improvements are welcome.
 *
 * This is a rewrite of backup.c and backwr.c, which have been
 * distributed on the internet for some time - with no authors listed.
 * This rewrite fixes many bugs, adds features, and has at most
 * a loose relationship to its predecessors.
 */

#ifndef MAGTAPE_H
#define MAGTAPE_H

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

typedef struct _MAGTAPE {
    char    *filename;
    uint32_t  filenum;
    uint32_t  blocknum;
    uint32_t  status;
#define MTS_ERROR      1
#define MTS_TM         2
#define MTS_EOM        4

#define MTS_WRITE      0x10000
#define MTS_METRIC     0x20000

    FILE    *fd;
    double  reellen;
    double  reelpos;
    double  eotpos;
    double density;
    double irg;
} MAGTAPE;

MAGTAPE *magtape_open( const char *filename, const char *mode );

int magtape_setsize( MAGTAPE *mta, const char *length, const char *density );

unsigned int magtape_read( MAGTAPE *mta, unsigned char *buffer, const size_t maxlen, uint32_t *recsize );

#define MTA_OK  0 /* Record read OK */
#define MTA_TM  1 /* Tape mark encountered */
#define MTA_EOF 2 /* EOF encountered (2 tape marks) */
#define MTA_ERR 3 /* Error in record data (e.g. data parity error on source tape ) */
#define MTA_EOM 4 /* End of medium */
#define MTA_IOE 5 /* IO/Error reading tape file */
#define MTA_FMT 6 /* Format error in tape file */
#define MTA_BTL 7 /* Block too large for buffer */

unsigned int magtape_write( MAGTAPE *mta, unsigned char *buffer, const size_t recsize );
#define MTA_DATA_ERROR(len) ((len) | 0x80000000)

typedef enum mta_marktype {
    MTA_EOF_MARK,
    MTA_GAP_MARK,
    MTA_EOM_MARK
} mta_marktype;
unsigned int magtape_mark( MAGTAPE *mta, mta_marktype type );

void magtape_pprintf( FILE *out, MAGTAPE *mta, int nl );

void magtape_close( MAGTAPE **mta );

#endif
