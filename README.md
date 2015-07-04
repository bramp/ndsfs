# ndsfs Nintendo DS ROM file system mounter (v1.1)
 by Andrew Brampton (bramp.net)
 using knowledge/code from ndstool by Rafael Vuijk (aka DarkFader)

## INTRO

Using [FUSE][1], a Nintendo DS ROM can be mounted as a file system. This tool is
similar to [ndstool][2], but allows you to browse the rom without extracting.

## COMPILE

    make

## RUN

    ndsfs <rom file> <mount point>

## TODO
 * Add extra checks to see if file_ids are valid
 * Implement cache so common paths aren't checked so often
 * Make sure this code works on big endian machines
 * Add write support (hard)
 * The mount directory looks really wierd unless you are a superuser
 * We have coarse grain locking, change this to be more fine grain.


[1]: http://fuse.sourceforge.net/
[2]: http://www.darkfader.net/ds/
