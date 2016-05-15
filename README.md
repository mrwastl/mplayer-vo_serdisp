mplayer-vo_serdisp
==================

Patch for mplayer to add support for serdisplib

This patch file was tested with mplayer svn version 37869.


Instructions
------------
* get recent svn source code for mplayer (see http://www.mplayerhq.hu/design7/dload.html)
* change into mplayer source directory
* apply patchfile `vo_serdisp.patch` from this repository (`patch -p0 < /directory_where_patchfile_is_stored/vo_serdisp.patch`)
* call `configure --enable-serdisp`
* build mplayer

*Note*:
* If mplayer is compiled using dynamic loading of shared libraries, presence of serdisplib is not required (neither shared object nor header files).
* If serdisplib is installed outside the library path or a different version (eg. svn version vs. packaged version) is to be used, use `LD_LIBRARY_PATH`
  (eg. `LD_LIBRARY_PATH=/path_to_serdisp/lib ./mplayer -vo serdisp:.....`).
* This repository also contains the source file for the vo driver `libvo/vo_serdisp.c`. It is already part of the patch file above and doesn't need to be copied to the mplayer source code.
