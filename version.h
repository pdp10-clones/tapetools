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
 */

#ifndef VERSION_H
#define VERSION_H

#ifndef VERSION_MAJOR
#define VERSION_MAJOR 0
#endif
#ifndef VERSION_MINOR
#define VERSION_MINOR 0
#endif
#ifndef VERSION_EDIT
#define VERSION_EDIT 12
#endif

#ifndef VERSION_CUST
#define VERSION_CUST 0
#endif

/* Version in 36-bit (lh,rh) format */

#define VERSION_36(maj,min,edt,cust) ((((maj)&0777)<<6)|((min)&077)|(((cust)&07)<<15)),(edt)

#define PRINT_VERSION(fd, prog) do {                                         \
        wd36_T version = { VERSION_36( VERSION_MAJOR,VERSION_MINOR,VERSION_EDIT,VERSION_CUST ) }; \
        char vbuf[VERSION_BUFFER_SIZE];                                 \
                                                                        \
        fprintf( fd, "%s %s\n", #prog, decodeversion( &version, vbuf ) ); \
    } while( 0 )

#endif
