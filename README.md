# `hidio`
`[hidio]` object for pure-data / vanilla Pd

* _This_ fork is based on Deken `hidio` library published on [Deken](https://deken.puredata.info/info?url=http%3A%2F%2Fpuredata.info%2FMembers%2Fmartinrp%2Fsoftware%2Fhidio%2F0.20200224%2Fhidio%5Bv0.20200224%5D%28Windows-amd64-32%29%28Sources%29.dek), and I can't find a repo to contribute back. As Martin passed away, he wouldn't be maintaining a repo anyway. It seems significantly different from the grandparent `[hidio]` 0.1 alpha and should be considered a new implementation.
* Martin Peach created the fork _this_ is based from, using code from https://github.com/Benitoite/hidio. That version was been built and partly tested with pd-0.50-2 using Msys64 on 64-bit Windows7.
* That is a fork of the `[hidio]` 0.1 alpha sourcecode from https://puredata.info/downloads/hidio.
* It has been built on a Windows 10 PC, targeting pd-0.50-2, and the build artifacts are included.  You may clone directly to your externals folder if you wish and start using `[hidio]` right away.  `[hidio]` is licensed under GPL2 and comes with ABSOLUTELY NO WARRANTY.
* PRs welcome.
* Issue a `make clean` first to build other platforms.

## Build instructions
* Install MSYS2
* Update the `Makefile` PDINCLUDEDIR variable to point to a cloned Pd repo or src directory where Pd is installed
* Open an MSYS2 MINGW64 environment
* pacman -S mingw-w64-x86_64-toolchain mingw-w64-x86_64-clang make autoconf automake libtool mingw-w64-x86_64-gettext
* make
<hr>

````
Project Description:

This is the next generation of HID API for Pd and Max/MSP.
The aim is to have this object perform the same on Pd on
GNU/Linux, Mac OS X, and Windows, and Max/MSP on Mac OS X
and Windows. 

Hans-Christoph Steiner <hans@eds.org> 
Olaf Matthes <olaf@nullmedium.de> 
David Merrill <dmerrill@media.mit.edu>
````
