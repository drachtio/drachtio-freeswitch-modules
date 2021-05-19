include $(top_srcdir)/build/modmake.rulesam
MODNAME=mod_aws_transcribe

mod_LTLIBRARIES = mod_aws_transcribe.la
mod_aws_transcribe_la_SOURCES  = mod_aws_transcribe.c aws_transcribe_glue.cpp
mod_aws_transcribe_la_CFLAGS   = $(AM_CFLAGS)
mod_aws_transcribe_la_CXXFLAGS = $(AM_CXXFLAGS) -std=c++11 -I${switch_srcdir}/libs/aws-sdk-cpp/aws-cpp-sdk-core/include -I${switch_srcdir}/libs/aws-sdk-cpp/aws-cpp-sdk-transcribestreaming/include -I${switch_srcdir}/libs/aws-sdk-cpp/build/.deps/install/include

mod_aws_transcribe_la_LIBADD   = $(switch_builddir)/libfreeswitch.la
mod_aws_transcribe_la_LDFLAGS  = -avoid-version -module -no-undefined -L/usr/local/lib \
-laws-cpp-sdk-transcribestreaming -laws-cpp-sdk-core -laws-crt-cpp \
-laws-c-s3 -laws-c-http -laws-c-auth \
-laws-c-mqtt -ls2n -laws-c-compression -laws-c-io -laws-c-cal -laws-c-event-stream \
-laws-checksums -laws-c-common  \
-lpthread -lcurl -lcrypto -lssl -lz
