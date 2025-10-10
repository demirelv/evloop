
target-y := evloop
evloop-cpp = y
evloop-source-y := main.cpp \
				../src/evloop.cpp \
				../src/timer.cpp \
				../src/poller.cpp

evloop-cppflags-y		:= -I../src/
evloop-ldflags-y	:= 

install-y	:= evloop:usr/bin/

include ../Build.mk
