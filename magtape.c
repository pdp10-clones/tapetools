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

/* This reads and writes SimH .TAP format files, with some
 * simplifying assumptions:
 *  No support for update mode (reading and writing the same tape)
 *  No support for tapes written with 1/2 gaps.
 */

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "magtape.h"

#ifndef MTA_MIN_RECORD_SIZE
#  define MTA_MIN_RECORD_SIZE 14
#endif

/* Tape format */

    /* Metadata: stored little-endian */

#define MT_EOM 0xFFFFFFFF /* End of medium */
#define MT_GAP 0xFFFFFFFE /* Erase gap */
#define MT_TM  0x00000000 /* Tape mark */
#define MT_RSVD(x) ( (((unsigned long) (x)) >= 0xFF000000) && (((unsigned long) (x)) <= 0xFFFFFFFD) ) 

    /* Data consists of length word, frames, length word.
     * Frames written is always even (MT_CNT +1) & ~1.
     * A padding frame is written if MT_CNT is odd.
     */
#define MT_ERR 0x80000000 /* Length field: record contains error */
#define MT_MBZ 0x7F000000 /* Length field: MBZ */
#define MT_CNT 0x00FFFFFF /* Length (frames) written */


MAGTAPE *magtape_open( const char *filename, const char *mode ) {
    MAGTAPE *mta;
    
    if( strcmp( mode, "r" ) && strcmp( mode, "w" ) ) {
        fprintf( stderr, "magtape_open: invalid mode %s\n", mode );
        exit( 1 );
    }

    mta = calloc( 1, sizeof( *mta ) );
    if( mta == NULL )
        return NULL;

    mta->filename = strdup( filename );
    if( mta->filename == NULL ) {
        free(mta);
        return NULL;
    }

    if( !strcmp( mode, "w" ) )
        mta->status |= MTS_WRITE;

    if( !strcmp( filename, "-" ) ) {
        mta->fd = ( (mta->status & MTS_WRITE)?
                    stdout: stdin );
    } else {
        mta->fd = fopen( filename,
                         ( (mta->status & MTS_WRITE)?
                           "wb": "rb" ) );
        if( mta->fd == NULL ) {
            free(mta->filename);
            free(mta);
            return NULL;
        }
    }

    return mta;
}

unsigned int magtape_read( MAGTAPE *mta, unsigned char *buffer, const size_t maxlen, uint32_t *recsize ) {

    *recsize = 0;

    if( mta->status & MTS_WRITE )
        abort();

    while( 1 ) {
        uint32_t rectype, endtype, length;
        uint8_t bytes[4];
        int n;
        unsigned int rc;

        if( mta->status & (MTS_ERROR | MTS_EOM) )
            return MTA_EOM;

        n = fread(bytes, sizeof(uint8_t), 4, mta->fd);
        if (n != 4) {
            mta->status |= MTS_ERROR;
            if( ferror( mta->fd ) )
                return MTA_IOE;
            if( n == 0 ) { /* This is EOF without an EOM marker */
                return MTA_EOM;
            }
            return MTA_FMT;
        }

        rectype = ((unsigned long) bytes[3] << 24) | ((unsigned long) bytes[2] << 16) |
            ((unsigned long) bytes[1] <<  8) | (bytes[0]);

        if (rectype == MT_TM) {
            mta->filenum++;
            if( mta->status & MTS_TM ) {
                mta->blocknum = 0;
                return MTA_EOF;
            }
            mta->status |= MTS_TM;
            return MTA_TM;
        }

        if( rectype == MT_GAP )
            continue;

        if( mta->status & MTS_TM ) {
            mta->status &= ~MTS_TM;
            mta->blocknum = 0;
        }

        if( rectype == MT_EOM ) {
            mta->status |= MTS_EOM;
            return MTA_EOM;
        }

        if( rectype & MT_MBZ || MT_RSVD(rectype) ) {
            mta->status |= MTS_ERROR;
            return MTA_FMT;
        }

        length = rectype & MT_CNT;

        mta->blocknum++;

        rc = MTA_OK;

        if( rectype & MT_ERR ) {
            rc = MTA_ERR;
        }

        if (length > maxlen ) {
            rc = MTA_BTL;
            *recsize = length;
            length = maxlen;
        } else {
            *recsize = length;
        }

        n = fread(buffer, sizeof(uint8_t), length, mta->fd);
        if( (size_t)n != length ) {
            *recsize = n;
            mta->status |= MTS_ERROR;
            if( ferror( mta->fd ) )
                return MTA_IOE;
            return MTA_FMT;
        }

        if( length & 1 ) {
            n = fread(bytes, sizeof(uint8_t), 1, mta->fd);
            if (n != 1) {
                mta->status |= MTS_ERROR;
                if( ferror( mta->fd ) )
                    return MTA_IOE;
                return MTA_FMT;
            }
        }

        n = fread(bytes, sizeof(uint8_t), 4, mta->fd);
        if (n != 4) {
            mta->status |= MTS_ERROR;
            if( ferror( mta->fd ) )
                return MTA_IOE;
            return MTA_FMT;
        }
        endtype = ((unsigned long) bytes[3] << 24) | ((unsigned long) bytes[2] << 16) |
                  ((unsigned long) bytes[1] <<  8) | (bytes[0]);

        if( endtype != rectype) {
            mta->status |= MTS_ERROR;
            return MTA_FMT;
        }

        if( length < MTA_MIN_RECORD_SIZE ) {
            fprintf( stderr, "Noise record (length = %" PRId32 ") at ", length );
            magtape_pprintf( stderr, mta, 1 );
            continue;
        }

        return rc;
    }
}

unsigned int magtape_write( MAGTAPE *mta, unsigned char *buffer, const size_t recsize ) {
    uint8_t bytes[4];
    size_t n;

    if( !(mta->status & MTS_WRITE) )
        abort();

    if( recsize & MT_MBZ ) {
        fprintf( stderr, "Record length too long\n" );
        exit( 1 );
    }

    bytes[0] = recsize & 0xFF;
    bytes[1] = (recsize >>  8) & 0xFF;
    bytes[2] = (recsize >> 16) & 0xFF;
    bytes[3] = 0;

    n = fwrite( bytes, sizeof( uint8_t ), 4, mta->fd );
    if( n != 4 ) {
        mta->status |= MTS_ERROR;
        return MTA_IOE;
    }

    n = fwrite( buffer, sizeof( uint8_t ), recsize, mta->fd );
    if( n != recsize ) {
        mta->status |= MTS_ERROR;
        return MTA_IOE;
    }

    n = fwrite( bytes, sizeof( uint8_t ), 4, mta->fd );
    if( n != 4 ) {
        mta->status |= MTS_ERROR;
        return MTA_IOE;
    }
    mta->blocknum++;

    return MTA_OK;
}

unsigned int magtape_mark( MAGTAPE *mta, mta_marktype type ) {
    uint8_t bytes[4];
    uint32_t code;
    int n;

    if( !(mta->status & MTS_WRITE) )
        abort();

    switch( type ) {
    case MTA_EOF_MARK:
        code = MT_TM;
        mta->blocknum = 0;
        mta->filenum++;
        break;
    case MTA_GAP_MARK:
        code = MT_GAP;
        break;
    case MTA_EOM_MARK:
        code = MT_EOM;
        break;
    default:
        abort();
    }
    bytes[0] = code & 0xFF;
    bytes[1] = (code >>  8) & 0xFF;
    bytes[2] = (code >> 16) & 0xFF;
    bytes[3] = (code >> 24) & 0xFF;

    n = fwrite( bytes, sizeof( uint8_t ), 4, mta->fd );
    if( n != 4 ) {
        mta->status |= MTS_ERROR;
        return MTA_IOE;
    }

    return MTA_OK;
}

void magtape_pprintf( FILE *out, MAGTAPE *mta, int nl ) {
    fprintf( out, "file %" PRIu32 ", record %" PRIu32 " of %s%s",
             mta->filenum, mta->blocknum, mta->filename, nl? "\n": "" );
}

void magtape_close( MAGTAPE **mta ) {
    if( mta[0]->status & MTS_WRITE ) {
        if( magtape_mark( *mta, MTA_EOM_MARK ) != MTA_OK )
            fprintf( stderr, "%s: %s\n", mta[0]->filename, strerror( errno ) );
    }

    if( fclose( mta[0]->fd ) )
        fprintf( stderr, "%s: %s\n", mta[0]->filename, strerror( errno ) );

    free( mta[0]->filename );
    free( *mta );

    *mta = NULL;

    return;
}
