
library-y := libevloop.so
libevloop.so-cpp = y
libevloop.so-source-y := evloop.cpp \
						 poller.cpp \
						 timer.cpp
libevloop.so-header-y := evloop.h timer.h poller.h

install-y	:= libevloop.so:usr/lib/
install-y	+= evloop.h:usr/include/
install-y	+= timer.h:usr/include/
install-y	+= poller.h:usr/include/

include ../Build.mk
