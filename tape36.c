/* Tape format conversions for 36-bit environments.
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
 */


#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <math.h>
#include <sys/types.h>
#include <string.h>

#include "magtape.h"
#include "data36.h"


static int tapemode( const char *name );
static const char *modename( const int mode );

typedef size_t (*packfn)(wd36_T *inbuf, size_t wc, uint8_t *outbuf);
typedef size_t (*unpackfn)(uint8_t *inbuf, size_t insize, wd36_T *outbuf);

static int convert( const char *infile, const int inmode, const char * outfile, const int outmode );

static size_t unpack_core_dump(uint8_t *inbuf, size_t insize, wd36_T *outbuf);
static size_t pack_core_dump(wd36_T *inbuf, size_t wc, uint8_t *outbuf);

/* For TAP files, 7-track is stored the right-justified in 8 bits.
 * Thus sixbit-7 and sixbit-9 happen to have the same encodings.
 */

static size_t unpack_sixbit_7(uint8_t *inbuf, size_t insize, wd36_T *outbuf);
static size_t pack_sixbit_7(wd36_T *inbuf, size_t wc, uint8_t *outbuf);
#define unpack_sixbit_9 unpack_sixbit_7
#define pack_sixbit_9 pack_sixbit_7

static size_t unpack_high_density(uint8_t *inbuf, size_t insize, wd36_T *outbuf);
static size_t pack_high_density(wd36_T *inbuf, size_t wc, uint8_t *outbuf);

static size_t unpack_industry(uint8_t *inbuf, size_t insize, wd36_T *outbuf);
static size_t pack_industry(wd36_T *inbuf, size_t wc, uint8_t *outbuf);

static size_t unpack_ansi_ascii(uint8_t *inbuf, size_t insize, wd36_T *outbuf);
static size_t pack_ansi_ascii(wd36_T *inbuf, size_t wc, uint8_t *outbuf);

static void usage( void );

#define MAXRECSIZE 0x00FFFFFF

typedef enum {
    CORE_DUMP,
    SIXBIT7,
    SIXBIT9,
    HIGH_DENSITY,
    INDUSTRY,
    ANSI_ASCII
} tapemode_T;

static struct tapemode {
    const char *const name;
    const int mode;
    const double fpw;
    packfn pack;
    unpackfn unpack;
    const char *const help;

} tapemodes[] = {
    { "core-dump",    CORE_DUMP,    5.0, pack_core_dump, unpack_core_dump,
      "9-Track native format, 5 frames/36-bit word" },
    { "sixbit-7",     SIXBIT7,      6.0, pack_sixbit_7, unpack_sixbit_7,
      "7-Track sixbit format, 6 frames/36-bit word" },
    { "sixbit-9",     SIXBIT9,      6.0, pack_sixbit_9, unpack_sixbit_9,
      "9-Track sixbit format, 6 frames/36-bit word" },
    { "sixbit",       SIXBIT9,      6.0, pack_sixbit_9, unpack_sixbit_9,
      "9-Track sixbit format, 6 frames/36-bit word" },
    { "high-density", HIGH_DENSITY, 4.5, pack_high_density, unpack_high_density,
      "9-Track high-density, 9 frames/72-bit doubleword" },
    { "industry",     INDUSTRY,     4.0, pack_industry, unpack_industry,
      "9-Track industry-compatible format,  4 frames/32-bit byte" },
    { "ansi-ascii",   ANSI_ASCII,   5.0, pack_ansi_ascii, unpack_ansi_ascii,
      "9-Track ANSI-ASCII format.  5 frames of 7-bit ASCII/36-bit word" },
    { NULL, 0, 0.0, NULL, NULL, NULL }
};

static int verbose = 0;

int main( int argc, char **argv) {
    char *infile = NULL,
        *outfile=NULL;
    int inmode = CORE_DUMP,
        outmode = CORE_DUMP;

    --argc;
    ++argv;

    while( argc && *argv[0] == '-' ) {
        char *sws = argv[0] +1;

        if( !strcmp( sws, "-" ) )
            break;

        if( !strcmp( sws, "-help" ) ) {
            usage();
            exit(0);
        }

        while( *sws ) {
            char *arg = NULL;

            if( strchr( "io", sws[0] ) ) { /* Switches with arguments */
                if( sws[1] ) {
                    arg = strdup( sws + 1 );
                    sws[1] = '\0';
                } else {
                    if( !argv[1] ) {
                        fprintf( stderr, "Missing argument for %c\n", sws[0] );
                        exit(1);
                    }
                    arg = argv++[1];
                    --argc;
                }
                switch( sws[0] ) {
                case 'i':
                    inmode = tapemode( arg );
                    break;
                case 'o':
                    outmode = tapemode( arg );
                    break;
                default:
                    abort();
                }
                break;
            }

            /* Switches that don't have arguments */

            switch( sws[0] ) {
            case 'v':
                verbose++;
                break;

            case 'h':
                usage();
                exit(0);

            default:
                fprintf( stderr, "Unknown switch %c\n", sws[0] );
                exit(1);
            }
            sws++;
        }
        argc--;
        argv++;
    }

    if( argc >= 1 ) {
        argc--;
        infile = argv++[0];
        if( argc >= 1 ) {
            argc--;
            outfile = argv++[0];
        } else {
            outfile = "-";
        }
    } else {
        infile = "-";
        outfile = "-";
    }

    exit(convert( infile, inmode, outfile, outmode ) );
}

static int convert( const char *infile, const int inmode, const char * outfile, const int outmode ) {
    MAGTAPE *in, *out;
    unsigned int status;
    uint32_t bytesread, recsize;
    packfn pack = NULL;
    unpackfn unpack = NULL;
    struct tapemode *mp;
    uint8_t *tapebuffer;
    wd36_T *tenbuffer;
    size_t tenbufsize = 0;
    int done = 0;

    for( mp = tapemodes; mp->name; mp++ ) {
        if( mp->mode == inmode ) {
            unpack = mp->unpack;
            tenbufsize =  sizeof( wd36_T ) * (size_t)
                ceil(((double)MAXRECSIZE / mp->fpw) );
        }
        if( mp->mode == outmode )
            pack = mp->pack;
    }
    if( !( pack && unpack ) )
        abort();

    in = magtape_open( infile, "r" );
    if( !in ) {
        fprintf( stderr, "%s: %s\n", infile, strerror( errno ) );
        return 1;
    }
    if( verbose )
        fprintf( stderr, "Reading %s in %s mode\n", infile, modename( inmode ) );

    out = magtape_open( outfile, "w" );
    if( !out ) {
        fprintf( stderr, "%s: %s\n", outfile, strerror( errno ) );
        return 1;
    }
    if( verbose )
        fprintf( stderr, "Writing %s in %s mode\n", outfile, modename( outmode ) );

    tapebuffer = malloc( MAXRECSIZE * sizeof( uint8_t ) );
    tenbuffer = malloc( tenbufsize );
    if( !(tapebuffer && tenbuffer) ) {
        fprintf( stderr, "Allocate buffer: %s\n", strerror( errno ) );
        return 1;
    }

    while( !done ) {
        int haserr = 0;

        status = magtape_read( in, tapebuffer, MAXRECSIZE, &bytesread );
        switch( status ) {
        case MTA_OK:
            break;
        case MTA_EOM:
            if( verbose ) {
                fprintf( stderr, "End of medium at " );
                magtape_pprintf( stderr, in, 1 );
            }
            done = 1;
            continue;
        case MTA_TM:
        case MTA_EOF:
            if( verbose ) {
                fprintf( stderr, "Tape mark at " );
                magtape_pprintf( stderr, in, 1 );
            }
            status = magtape_mark( out, MTA_EOF_MARK );
            if( status != MTA_OK ) {
                fprintf( stderr, "Error writing tape mark: %s at ", strerror( errno ) );
                magtape_pprintf( stderr, out, 1 );
                done = 1;
            }
            continue;
        case MTA_ERR:
            haserr = 1;
            break;
        case MTA_IOE:
            fprintf( stderr, "Error reading tape file: %s at ", strerror( errno ) );
            magtape_pprintf( stderr, in, 1 );
            done = 1;
            continue;
        case MTA_FMT:
            fprintf( stderr, "Input tape file format error at " );
            magtape_pprintf( stderr, in, 1 );
            done = 1;
            continue;
        case MTA_BTL: /* Can't happen - maximum size was allocated... */
        default:
            abort();
        }
        recsize = unpack(tapebuffer, bytesread, tenbuffer );
        if( recsize == (size_t)-1 ) {
            fprintf( stderr, "Invalid record size %" PRIu32 " for input mode at ", bytesread );
            magtape_pprintf( stderr, in, 1 );
            break;
        }
        recsize = pack( tenbuffer, recsize, tapebuffer );
        if( haserr )
            recsize = MTA_DATA_ERROR( recsize );
        status = magtape_write( out, tapebuffer, recsize );
        switch( status ) {
         case MTA_OK:
            break;
         case MTA_IOE:
            fprintf( stderr, "Error writing tape file: %s at ", strerror( errno ) );
            magtape_pprintf( stderr, out, 1 );
            done = 1;
            continue;
        default:
            abort();
        }
    }

    if( verbose ) {
        fprintf( stderr, "Completed\n" );
        fprintf( stderr, "Input: %s", infile );
    }
    magtape_close( &in );
    magtape_close( &out );

    free(tenbuffer);
    free(tapebuffer);

    return 0;
}

static size_t unpack_core_dump(uint8_t *inbuf, size_t insize, wd36_T *outbuf) {
    size_t wc = 0;

    if( insize % 5 )
        return (size_t)-1;

    insize /= 5;
    wc = insize;
    while( insize-- != 0 ) {
        outbuf->lh =
            (inbuf[0] << 10) |
            (inbuf[1] << 2)  |
            (inbuf[2] >> 6);
        outbuf->rh =
            ((inbuf[2] & 077) << 12) |
            (inbuf[3] << 4) |
            (inbuf[4] & 017);
        inbuf += 5;
        outbuf++;
    }

    return wc;
}

static size_t pack_core_dump(wd36_T *inbuf, size_t wc, uint8_t *outbuf) {
    size_t bc;

    bc = wc * 5;
    while( wc-- != 0 ) {
        *outbuf++ = inbuf->lh >> 10;
        *outbuf++ = inbuf->lh >> 2;
        *outbuf++ = ((inbuf->lh << 6) & 0300) | ((inbuf->rh >> 12) & 077);
        *outbuf++ = inbuf->rh >> 4;
        *outbuf++ = inbuf++->rh & 017;
    }

    return bc;
}

static size_t unpack_sixbit_7(uint8_t *inbuf, size_t insize, wd36_T *outbuf) {
    size_t wc = 0;

    if( insize % 6 )
        return (size_t)-1;

    insize /= 6;
    wc = insize;
    while( insize-- != 0 ) {
        outbuf->lh =
            ((inbuf[0] & 077) << 12) |
            ((inbuf[1] & 077) << 6)  |
            (inbuf[2] & 077);
        outbuf->rh =
            ((inbuf[3] & 077) << 12) |
            ((inbuf[4] & 077) << 6)  |
            (inbuf[5] & 077);
        inbuf += 6;
        outbuf++;
    }

    return wc;
}

static size_t pack_sixbit_7(wd36_T *inbuf, size_t wc, uint8_t *outbuf) {
    size_t bc;

    bc = wc * 6;
    while( wc-- != 0 ) {
        *outbuf++ = 077 & (inbuf->lh >> 12);
        *outbuf++ = 077 & (inbuf->lh >> 6);
        *outbuf++ = 077 & (inbuf->lh);
        *outbuf++ = 077 & (inbuf->rh >> 12);
        *outbuf++ = 077 & (inbuf->rh >> 6);
        *outbuf++ = 077 & (inbuf->rh);
    }

    return bc;
}

static size_t unpack_high_density(uint8_t *inbuf, size_t insize, wd36_T *outbuf) {
    size_t wc = 0;

    if( insize % 9 )
        return (size_t)-1;

    insize /= 9;
    wc = insize * 2;
    while( insize-- != 0 ) {
        outbuf->lh =
            (inbuf[0] << 10) |
            (inbuf[1] << 2)  |
            (inbuf[2] >> 6);
        outbuf++->rh =
            ((inbuf[2] & 077) << 12) |
            (inbuf[3] << 4) |
            ((inbuf[4] & 0360) >> 4);
        outbuf->lh =
            ((inbuf[4] & 017) << 14) |
            (inbuf[5] << 6)  |
            ((inbuf[6] & 0374) >> 2);
        outbuf++->rh =
            ((inbuf[6] & 03) << 16) |
            (inbuf[7] << 8) |
            inbuf[8];
        inbuf += 9;
    }

    return wc;
}

static size_t pack_high_density(wd36_T *inbuf, size_t wc, uint8_t *outbuf) {
    size_t bc;

    bc = (((wc +1) & ~1) * 9) / 2;

    while( wc-- != 0 ) {
        *outbuf++ = (inbuf->lh >> 10);
        *outbuf++ = (inbuf->lh >> 2);
        *outbuf++ = ((inbuf->lh & 03) << 6) | ((inbuf->rh >> 12) & 077);
        *outbuf++ = (inbuf->rh >> 4);
        if( wc == 0 ) {
            *outbuf++ = ((inbuf->rh & 017) << 4);
            *outbuf++ = 0;
            *outbuf++ = 0;
            *outbuf++ = 0;
            *outbuf++ = 0;
            break;
        }
        *outbuf++ = ((inbuf[0].rh & 017) << 4) |
                    ((inbuf[1].lh & 0740000) >> 14);
        inbuf++;
        *outbuf++ = (inbuf->lh >> 6);
        *outbuf++ = ((inbuf->lh & 077) << 2) | ((inbuf->rh >> 16) & 03);
        *outbuf++ = (inbuf->rh >> 8);
        *outbuf++ = inbuf->rh;
        inbuf++;
    }

    return bc;
}

static size_t unpack_industry(uint8_t *inbuf, size_t insize, wd36_T *outbuf) {
    size_t wc = 0;

    if( insize % 4 )
        return (size_t)-1;

    insize /= 4;
    wc = insize;
    while( insize-- != 0 ) {
        outbuf->lh =
            ((inbuf[0] & 0377) << 10) |
            ((inbuf[1] & 0377) << 2)  |
            (inbuf[2] >> 6);
        outbuf++->rh =
            ((inbuf[2] & 077) << 12) |
            ((inbuf[3] & 0377) << 4);
        inbuf += 4;
    }

    return wc;
}

static size_t pack_industry(wd36_T *inbuf, size_t wc, uint8_t *outbuf) {
    size_t bc;

    bc = wc * 4;

    while( wc-- ) {
        outbuf = decode8ascii( inbuf++, outbuf );

    }

    return bc;
}

static size_t unpack_ansi_ascii(uint8_t *inbuf, size_t insize, wd36_T *outbuf) {
    size_t wc = 0;

    if( insize % 5 )
        return (size_t)-1;

    insize /= 5;
    wc = insize;

    while( insize-- != 0 ) {
        uint32_t bit35;

        bit35 = ((inbuf[0] | inbuf[1] | inbuf[2] | inbuf[3]) >> 7) & 1;

        outbuf->lh =
            ((inbuf[0] & 0177) << 11) |
            ((inbuf[1] & 0177) << 4)  |
            ((inbuf[2] & 0170) >> 3);
        outbuf++->rh =
            ((inbuf[2] & 07) << 15) |
            ((inbuf[3] & 0177) << 8) |
            ((inbuf[4] & 0177) << 1) | bit35;
        inbuf += 5;
    }

    return wc;
}

static size_t pack_ansi_ascii(wd36_T *inbuf, size_t wc, uint8_t *outbuf) {
    size_t bc;

    bc = wc * 5;

    while( wc-- ) {
        outbuf = decode7ascii( inbuf, outbuf );
        if( inbuf++->rh & 1 )
            outbuf[-1] |= 0200;
    }

    return bc;

}

static int tapemode( const char *name ) {
    struct tapemode *p;

    if( name != NULL ) {
        for( p = tapemodes; p->name; p++ ) {
            if( !strcasecmp( name, p->name ) ) {
                return p->mode;
            }
        }
    }
    fprintf( stderr, "Valid tape formats are:\n" );
    for( p = tapemodes; p->name; p++ ) {
        fprintf( stderr, "    %-15s %s\n", p->name, p->help );
    }
    if( name )
        exit( 1 );

    return -1;
}
 
static const char *modename( const int mode ) {
    struct tapemode *p;

    for( p = tapemodes; p->name; p++ ) {
        if( p->mode == mode )
            return p->name;
    }
    abort();
}

static void usage( void ) {

    fprintf( stderr, "tape36 [-i mode] [-o mode] [-v] [-h] [infile [outfile]]\n" );
    fprintf( stderr, "\n" );
    fprintf( stderr, "Convert .tap from PDP-10 one data packing format to another\n" );
    fprintf( stderr, "\n" );
    fprintf( stderr, "-i specify input file format\n" );
    fprintf( stderr, "-o specify output file format\n" );
    fprintf( stderr, "-v provide processing details\n" );
    fprintf( stderr, "-h this usage\n" );
    fprintf( stderr, "\n" );
    fprintf( stderr, "infile and outfile default to stdin and stdout\n" );
    fprintf( stderr, "input and output modes default to core-dump\n" );
    fprintf( stderr, "\n" );
    tapemode( NULL );

    return;
}

/* EOF */
