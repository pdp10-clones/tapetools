/* Tape format conversions for 36-bit environments.
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
#include "version.h"

#define MAXRECSIZE 0x00FFFFFF
#define RECBUFSIZE (MAXRECSIZE * sizeof( uint8_t ))

typedef enum {
    CORE_DUMP,
    SIXBIT7,
    SIXBIT9,
    HIGH_DENSITY,
    INDUSTRY,
    ANSI_ASCII
} tapemode_T;

static tapemode_T tapemode( const char *name );
static const char *modename( const tapemode_T mode );

static int convert( const char *infile, const tapemode_T inmode,
                    const char *outfile, const tapemode_T outmode,
                    const char *density, const char *reelsize);

static void usage( void );

static struct tapemode {
    const char *const name;
    tapemode_T mode;
    const double fpw;
    packfn_T pack;
    unpackfn_T unpack;
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
        *outfile=NULL,
        *density = NULL,
        *reelsize = NULL;
    tapemode_T inmode = CORE_DUMP,
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
        if( !strcmp( sws, "-version" ) ) {
            PRINT_VERSION( stderr, tape36 );
            exit(0);
        }

        while( *sws ) {
            char *arg = NULL;

            if( strchr( "dior", sws[0] ) ) { /* Switches with arguments */
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
                case 'd':
                    density = arg;
                    break;
                case 'i':
                    inmode = tapemode( arg );
                    break;
                case 'o':
                    outmode = tapemode( arg );
                    break;
                case 'r':
                    reelsize = arg;
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

    exit(convert( infile, inmode, outfile, outmode, density, reelsize ) );
}

static int convert( const char *infile, const tapemode_T inmode,
                    const char *outfile, const tapemode_T outmode,
                    const char *density, const char *reelsize) {
    MAGTAPE *in, *out;
    unsigned int status;
    uint32_t bytesread, recsize;
    packfn_T pack = NULL;
    unpackfn_T unpack = NULL;
    struct tapemode *mp;
    uint8_t *tapebuffer;
    wd36_T *tenbuffer;
    size_t tenbufsize = 0;
    size_t maxwc = 0;

    int done = 0;

    for( mp = tapemodes; mp->name; mp++ ) {
        if( mp->mode == inmode ) {
            unpack = mp->unpack;
            tenbufsize =  sizeof( wd36_T ) * (size_t)
                ceil(((double)MAXRECSIZE / mp->fpw) );
            maxwc = tenbufsize / sizeof( wd36_T );
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
    if( density || reelsize ) {
        if( magtape_setsize( out, reelsize, density ) != 0 ) {
            fprintf( stderr, "Invalid reel size or density\n" );
            return 1;
        }
        magtape_setsize( in, reelsize, density );
    }

    if( verbose )
        fprintf( stderr, "Writing %s in %s mode\n", outfile, modename( outmode ) );

    tapebuffer = malloc( RECBUFSIZE );
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
        recsize = unpack(tapebuffer, bytesread, tenbuffer, maxwc );
        if( recsize == (size_t)-1 ) {
            fprintf( stderr, "Record size %" PRIu32 " is invalid for %s input at ", bytesread, modename( inmode ) );
            magtape_pprintf( stderr, in, 1 );
            break;
        }
        recsize = pack( tenbuffer, recsize, tapebuffer, RECBUFSIZE );
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

        case MTA_EOM:
        default:
            abort();
        }
    }

    if( verbose ) {
        fprintf( stderr, "Completed\n" );
        fprintf( stderr, "Input:  at " );
        magtape_pprintf( stderr, in, 1 );
        fprintf( stderr, "Output: at " );
        magtape_pprintf( stderr, out, 1 );
    }
    magtape_close( &in );
    magtape_close( &out );

    free(tenbuffer);
    free(tapebuffer);

    return 0;
}

static tapemode_T tapemode( const char *name ) {
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
 
static const char *modename( const tapemode_T mode ) {
    struct tapemode *p;

    for( p = tapemodes; p->name; p++ ) {
        if( p->mode == mode )
            return p->name;
    }
    abort();
}

static void usage( void ) {

    fprintf( stderr, "tape36 [-i mode] [-o mode] [-d dens] [-r len] [-v] [-h] [infile [outfile]]\n" );
    fprintf( stderr, "\n" );
    fprintf( stderr, "Convert .tap from PDP-10 one data packing format to another\n" );
    fprintf( stderr, "\n" );
    fprintf( stderr, "-i specify input file format\n" );
    fprintf( stderr, "-o specify output file format\n" );
    fprintf( stderr, "-d specify tape density (800,1600, 6250, etc)\n" );
    fprintf( stderr, "-r specify reel size (2400ft, 732m)\n" );
    fprintf( stderr, "-v provide processing details\n" );
    fprintf( stderr, "-h this usage\n" );
    fprintf( stderr, "\n" );
    fprintf( stderr, "infile and outfile default to stdin and stdout\n" );
    fprintf( stderr, "input and output modes default to core-dump\n" );
    fprintf( stderr, "Density and length estimate linear position. They are optional.\n" );
    fprintf( stderr, "\n" );
    tapemode( NULL );

    return;
}

/* EOF */
