# `hidio`
`[hidio]` object for pure-data / vanilla Pd

* This library provides an HIDIO object which can be used to connect a USB HID device.
* It was necessary to update this library because `get_device_number_by_id` was not implemented for Windows and therefore you could not connect to a USB device using the VID and PID, unlike the Linux or MacOS implementations.
* _This_ fork is based on Deken `hidio` library published on [Deken](https://deken.puredata.info/info?url=http%3A%2F%2Fpuredata.info%2FMembers%2Fmartinrp%2Fsoftware%2Fhidio%2F0.20200224%2Fhidio%5Bv0.20200224%5D%28Windows-amd64-32%29%28Sources%29.dek), and I can't find a repo to contribute back. As Martin passed away, he wouldn't be maintaining a repo anyway. It seems significantly different from the grandparent `[hidio]` 0.1 alpha and should be considered a new implementation.
* Martin Peach created the fork _this_ is based from, using code from https://github.com/Benitoite/hidio. That version was been built and partly tested with pd-0.50-2 using Msys64 on 64-bit Windows7.
* That is a fork of the `[hidio]` 0.1 alpha sourcecode from https://puredata.info/downloads/hidio.
* It has been built with MSYS2 on a Windows 10 PC targeting pd-0.50-2, and with WSL on a Windows 10 PC targeting an installed version of Pd 0.55.1. The build artifacts for the 0.55.1 build are included, but either compiled .dll seemed to work with Pd 0.55.1. You may clone directly to your externals folder if you wish and start using `[hidio]` right away.  `[hidio]` is licensed under GPL2 and comes with ABSOLUTELY NO WARRANTY.
* PRs welcome.
* Issue a `make clean` first to build other platforms.

## Build instructions
### MSYS2
* Install MSYS2
* Update the `Makefile` PDINCLUDEDIR variable to point to a cloned Pd repo or src directory where Pd is installed
* I was only successful using the pd-0.50-2 source rather than the Pd 0.50.2 installation headers
* Open an MSYS2 MINGW64 environment
* `pacman -S mingw-w64-x86_64-toolchain mingw-w64-x86_64-clang make autoconf automake libtool mingw-w64-x86_64-gettext`
* `make`

### WSL
* Install a WSL environment
* Update the `Makefile` variables to point to a cloned Pd repo or src directory where Pd is installed
* Open a WSL environment
* `sudo apt update && sudo apt upgrade -y`
* `sudo apt-get install build-essential automake autoconf libtool gettext`
* `sudo apt-get install mingw-w64 mingw-w64-tools`
* `sudo apt-get install nsis`
* Download [xwin](https://github.com/Jake-Shadle/xwin) - the latest version at the time this was tested can be had with: `wget https://github.com/Jake-Shadle/xwin/releases/download/0.6.5/xwin-0.6.5-x86_64-unknown-linux-musl.tar.gz`
* Untar it in a location you can access: `tar -xzvf xwin-0.6.5-x86_64-unknown-linux-musl.tar.gz`
* Create a symbolic link: `ln -s xwin-0.6.5-x86_64-unknown-linux-musl/xwin bin/xwin`
* Splat the SDK files where they will be accessible: `bin/xwin --accept-license splat --output dev/xwin`
* Update the `Makefile` cflags variable to point to the Windows SDK
* `make`

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
