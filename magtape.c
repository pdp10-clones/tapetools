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

    /* Parameters for linear position estimation.
     * These are (mostly) in feet, though the position is maintained in inches.
     * The standards specify minimums.  Most may be longer due to drive
     * tolerances and/or tape errors.  The SimH format doesn't know
     * about density or linear position, but they are needed to emulate
     * the EOT marker.  The EOT marker is a write warning, used to initiate
     * a volume switch.
     */

#define BOT_POS 16.0      /* Feet prior to BOT marker (14-18 ft) */
#define EOT_POS 27.5      /* Feet after EOT marker (25-30ft) */
#define MIN_LENGTH (BOT_POS + EOT_POS + 1.0) /* Minimum tape length */
#define TM_LENGTH 3.5     /* Length of a tape mark or erase gap (in) */

static int update_pos( MAGTAPE *mta, const double distance );

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
    mta->reellen = 0.0;
    mta->reelpos = 0.0;

    return mta;
}

int magtape_setsize( MAGTAPE *mta, const char *length, const char *density ) {
    unsigned long denv, lenv;
    char *endp;

    mta->reellen = 0.0;
    mta->reelpos = 0.0;
    mta->density = 1.0;

    if( length && !density )
        density = "800";
    if( density && !length )
        length = "2400ft";
    if( !(density && length) )
        return 0;

    denv = strtoul( density, &endp, 10 );
    if( denv == 0 || endp == density )
        return 1;
    if( *endp ) {
        if( strcasecmp( endp, "bpi" ) && strcasecmp( endp, "fci" ) )
            return 1;
    }

    lenv = strtoul( length, &endp, 10 );
    if( denv == 0 || endp == length )
        return 1;
    if( *endp ) {
        if( !strcasecmp( endp, "m" ) ) {
            mta->reellen = 39.3701 * (double)lenv;
            mta->status |= MTS_METRIC;
        } else if( strcasecmp( endp, "ft" ) ) {
            return 1;
        } else {
            mta->reellen = 12.0 * (double)lenv;
        }
    } else {
        mta->reellen = 12.0 * (double)lenv;
    }
    if( mta->reellen < (MIN_LENGTH * 12.0) )
        return 1;

    mta->reelpos = BOT_POS * 12.0;
    mta->eotpos = mta->reellen - (EOT_POS * 12.0);

    mta->density = (double)denv;
    /* Standard IRG (which doesn't include the 9 characters of CRC & LPCC)
     * is minimum: 0.3in for 9-track >= 6350, 0.6 in 9-track < 6250, and 0.75
     * for 7-track.  7-Track has LPCC, but no CRC.
     */
    if( denv >= 6250 )
        mta->irg = 0.3;
    else
        mta->irg = 0.6;
    return 0;
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
            mta->reelpos += 3.0;
            if( mta->status & MTS_TM ) {
                mta->blocknum = 0;
                return MTA_EOF;
            }
            mta->status |= MTS_TM;
            return MTA_TM;
        }

        if( rectype == MT_GAP ) {
            (void) update_pos( mta, TM_LENGTH );
            continue;
        }

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

        if( mta->reellen )
            (void) update_pos( mta, mta->irg +
                               ((double)(length+9) / mta->density) );

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

    if( mta->status & MTS_EOM )
        return MTA_EOM;

    if( recsize & MT_MBZ ) {
        fprintf( stderr, "Record too long (%" PRId32 ") for TAP format\n", recsize );
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
    if( mta->reellen )
            (void) update_pos( mta, mta->irg +
                               ((double)(recsize+9) / mta->density) );

    if( mta->status & MTS_EOT ) {
        mta->status &= ~MTS_EOT;
        return MTA_EOT;
    }
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
        (void) update_pos( mta, TM_LENGTH );
        break;
    case MTA_GAP_MARK:
        code = MT_GAP;
        (void) update_pos( mta, TM_LENGTH );
        break;
    case MTA_EOM_MARK:
        code = MT_EOM;
        mta->status |= MTS_EOM;
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
    fprintf( out, "file %" PRIu32 ", record %" PRIu32, mta->filenum, mta->blocknum );
    if( mta->reellen ) {
        if( mta->status & MTS_METRIC )
            fprintf( out, " (%.1fm)", mta->reelpos / 39.3701 );
        else
            fprintf( out, " (%.1fft)", mta->reelpos / 12 );
    }
    fprintf( out, " of %s%s",
              mta->filename, nl? "\n": "" );
}

static int update_pos( MAGTAPE *mta, const double distance ) {
    double oldpos;

    if( !mta->reellen )
        return 0;

    oldpos = mta->reelpos;
    mta->reelpos += distance;
    if( oldpos < mta->eotpos && mta->reelpos >= mta->eotpos ) {
        mta->status |= MTS_EOT;
        return 1;
    }
    return 0;
}

void magtape_close( MAGTAPE **mta ) {
    if( (mta[0]->status & MTS_WRITE) && !(mta[0]->status & MTS_EOM) ) {
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
