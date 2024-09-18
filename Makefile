# Makefile to build class 'hidio' for Pure Data.
# Needs Makefile.pdlibbuilder as helper makefile for platform-dependent build
# settings and rules.

# Mingw
#HOMEPATHMOUNT=/c

# WSL
# enable to build under WSL
HOMEPATHMOUNT=/mnt/c
system = Windows
cflags = -I ~/dev/xwin/sdk/include
CC = x86_64-w64-mingw32-gcc

HOMEPATH=$(HOMEPATHMOUNT)/Users/RBees
APPDATA=$(HOMEPATH)/AppData/Roaming
ProgramFiles=$(HOMEPATHMOUNT)/Program\ Files
ProgramFiles(x86)=$(HOMEPATHMOUNT)/Program\ Files\ (x86)
ProgramW6432=$(HOMEPATHMOUNT)/Program\ Files
PDDIR=$(ProgramFiles)/Pd
PDINCLUDEDIR=$(PDDIR)/src
PDBINDIR=$(PDDIR)/bin
PDLIBDIR=$(APPDATA)/Pd

# library name
lib.name = hidio

# input source file (class name == source file basename)
hidio.class.sources = hidio_windows.c hidio_linux.c hidio_darwin.c hidio_types.c input_arrays.c hidio.c 

# all extra files to be included in binary distribution of the library
datafiles = hidio-help.pd README.md
ldlibs = -lhid -lsetupapi

# include Makefile.pdlibbuilder from submodule directory 'pd-lib-builder'
PDLIBBUILDER_DIR=pd-lib-builder/
include $(PDLIBBUILDER_DIR)/Makefile.pdlibbuilder

.PHONY: list
list:
	@LC_ALL=C $(MAKE) -pRrq -f $(firstword $(MAKEFILE_LIST)) : 2>/dev/null | awk -v RS= -F: '/(^|\n)# Files(\n|$$)/,/(^|\n)# Finished Make data base/ {if ($$1 !~ "^[#.]") {print $$1}}' | sort | grep -E -v -e '^[^[:alnum:]]' -e '^$@$$'
# IMPORTANT: The line above must be indented by (at least one) 
#            *actual TAB character* - *spaces* do *not* work.