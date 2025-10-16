#####
PROGNAME:=hostDriver
#####

TT_METAL_BASEDIR:=/data/ackley/PART4/code/D/tt-metal/tt_metal
TT_3RDPARTY_BASEDIR:=$(TT_METAL_BASEDIR)/third_party
UMD_DEVICE_BASEDIR:=$(TT_3RDPARTY_BASEDIR)/umd/device
API_BASEDIR:=$(UMD_DEVICE_BASEDIR)/api
BUILDRELEASE_BASEDIR:=/data/ackley/PART4/code/D/tt-metal/build_Release
#Thu Sep 25 13:21:04 2025 Don't need anymore? UMD_INCLUDES+=-I$(UMD_DEVICE_BASEDIR)
UMD_INCLUDES+=-I$(BUILDRELEASE_BASEDIR)/include
#Thu Sep 25 13:21:44 2025 Ditto? UMD_INCLUDES+=-I$(API_BASEDIR)
UMD_LIBDIRS+=-L$(BUILDRELEASE_BASEDIR)/lib
#UMD_LIBDIRS+=-L$(BUILDRELEASE_BASEDIR)/tt_metal/third_party/umd
UMD_LIBS+=-ldevice
UMD_DLLPATHS+=-Wl,-R/data/ackley/PART4/code/D/tt-metal/build_Release/lib
#UMD_INCLUDES+=-I$(TT_UMD_BASEDIR)/common
#UMD_INCLUDES+=-I$(TT_UMD_BASEDIR)/device
