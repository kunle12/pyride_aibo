#
# Copyright 2003 Sony Corporation 
#
# Permission to use, copy, modify, and redistribute this software for
# non-commercial use is hereby granted.
#
# This software is provided "as is" without warranty of any kind,
# either expressed or implied, including but not limited to the
# implied warranties of fitness for a particular purpose.
#

OPENRSDK_ROOT?=/opt/openrsdk
INSTALLDIR=../MS/OPEN-R/MW/OBJS
STRIP=$(OPENRSDK_ROOT)/bin/mipsel-linux-strip
MKBIN=$(OPENRSDK_ROOT)/OPEN_R/bin/mkbin
STUBGEN=$(OPENRSDK_ROOT)/OPEN_R/bin/stubgen2
MKBINFLAGS=-p $(OPENRSDK_ROOT)
RANLIB=ranlib
AR=ar
ARCH=$(shell uname -a)

LIBS=-L$(OPENRSDK_ROOT)/OPEN_R/lib -lObjectComm -lOPENR -lInternet -lantMCOOP \
	 -L../libsrc/python27 -lpython2.7  -L../libsrc/openssl -lcrypto

CXX=$(OPENRSDK_ROOT)/bin/mipsel-linux-g++
CXXFLAGS= \
	-O2 \
	-g \
	-I. \
	-I$(OPENRSDK_ROOT)/OPEN_R/include/R4000 \
	-I$(OPENRSDK_ROOT)/OPEN_R/include/MCOOP	\
	-I$(OPENRSDK_ROOT)/OPEN_R/include	\
	-I../libsrc/python27				\
	-I../libsrc/python27/Include \
	-I../libsrc/openssl/include 	\
	-I../libsrc/pyconnect

OBJS= \
	../libsrc/pyconnect/PyConnectCommon.o \
	../libsrc/pyconnect/PyConnectObjComm.o \
	../libsrc/pyconnect/PyConnectInterNetComm.o \
	../libsrc/pyconnect/PyConnectInterObjComm.o \
	../libsrc/pyconnect/PyConnectStub.o

#
# When OPENR_DEBUG is defined, OSYSDEBUG() is available.
#
CXXFLAGS+= -DPYTHON_SERVER -DMULTI_SERVERS -DOPENR_DEBUG -DOPENR_LOG -DOPENR_OBJECT

.PHONY: all install clean

all: PythonServer.bin

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -o $@ -c $^

%.o: %.cc
	$(CXX) $(CXXFLAGS) -o $@ -c $^

PythonServerStub.cc: stub.cfg
	$(STUBGEN) stub.cfg	

PythonServer.bin: PythonServerStub.o PythonServer.o PythonServer.ocf $(OBJS) 
	$(MKBIN) $(MKBINFLAGS) -o $@ $^ $(LIBS)
	$(STRIP) $@

$(LIBRARY): $(OBJS)
	-rm -f $@
	$(AR) cr $@ $^
	$(RANLIB) $@

install: PythonServer.bin
	gzip -c PythonServer.bin > $(INSTALLDIR)/PYSEV.BIN

clean:
	rm -f *.a
	rm -f *.o *.bin *.elf *.snap.cc
	rm -f PythonServerStub.h PythonServerStub.cc def.h entry.h
	rm -f $(INSTALLDIR)/PYSEV.BIN
