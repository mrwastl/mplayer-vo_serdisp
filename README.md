mplayer-vo_serdisp
==================

Patch for mplayer to add support for serdisplib

This patch file was tested with mplayer svn version 37340.


Instructions
------------
* get recent svn source code for mplayer (see http://www.mplayerhq.hu/design7/dload.html)
* change into mplayer source directory
* apply patchfile `vo_serdisp.patch` from this repository (`patch -p0 < /directory_where_patchfile_is_stored/vo_serdisp.patch`)
* call `configure --enable-serdisp`
* build mplayer


This repository also contains the source file for the vo driver `libvo/vo_serdisp.c`. It is already part of the patch file above and doesn't need to be copied to the mplayer source code.

