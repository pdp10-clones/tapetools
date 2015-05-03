# backup36
DECsystem-10 backup utility for POSIX environments

This repository contains a C-language utility for reading 
and creating TOPS-10 BACKUP (and TOPS-20 DUMPER Interchange)
format tapes, in SimH format.

It also contains a utility for converting SimH format tapes
in any of the PDP-10 encoding modes to any other encoding mode.
This allows you to reformat an image of a high-density format
tape (which SimH does not support) into a core-dump format tape
(which SimH supports).

Conversions are not necessarily lossless.  They are done as if
a PDP-10 read the tape in one mode, and wrote it in the other.

See the PDP-10 hardware documentation and the TOPS-10 and TOPS-20
Monitor Calls manuals for details.

This is currently a work-in progress.

To build, use:

  make tape36

The default is to make all, which includes backup36.  This will
not work at present, as backup36 has not been released.
