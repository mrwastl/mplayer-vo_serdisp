mplayer-vo_serdisp
==================

Patch for mplayer to add support for serdisplib

This patch file was tested with mplayer svn version 38034.


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

Usage
-----
`mplayer -vo 'serdisp:name=<driver>{":"option}' file`

#### Options
* **name** (required)  
  driver-name in serdisplib  
  *example*: `name=sdl`, `name=rs232`
* **device** (optional, if not given, default device for *`<name>`* is used (supported since *serdisplib >= 2.01*))  
  device string  
  *example*: `device=out?`, `device=USB?FTDI/DLP2232M`, `device=RS232?/dev/usb/ttyUSB0`  
  **NOTE**: all `':'` need to be replaced by `'?'` because mplayer uses `':'` as a separator
* **options** (optional)  
  options string  
  *example*: `options=brightness=30;rot=90`
* **backlight** (default: `1`)  
  `0`: switch backlight off  
  `1`: switch backlight on  
  *example*: `backlight=0`
* **viewmode** (default: `0`)  
  `0` : normal (fit video into screen)  
  `1` : fit only width into screen (height might be clipped)  
  `2` : fit only height into screen (width might be clipped)  
* **debug** (default: `0`)  
  `0`: no debug information  
  `1`: print debug information

Options only applicable when using monochrome or greyscale displays:
* **dither** (default: `1`)  
  `0` : threshold  
  `1` : floyd steinberg
* **threshold** (only valid for monochrome displays, default: `127`)  
  threshold value for threshold dithering, value in `[0, 255]`
* **bandpass** (`default: 30`)  
  bandpass value for floyd steinberg dithering, value in `[0, 255]`
* **gamma** (default: `1.0`)  
  gamma correction  
  *example*: `gamma=1.5`

### Examples
`mplayer -vo 'serdisp:name=sdl:device=out?:viewmode=1:options=brightness=30' movie.mp4`

`mplayer -vo 'serdisp:name=sdl:options=w=1024;h=768' movie.mp4`

`mplayer -vo 'serdisp:name=n3510i:device=USB?FTDI/DLP2232M' movie.mp4`
