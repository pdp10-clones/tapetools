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
#define VERSION_EDIT 10
#endif

#define VERSION_STRING(prog) "%s version %d.%d(%d)\n", #prog, VERSION_MAJOR,VERSION_MINOR,VERSION_EDIT

#endif
