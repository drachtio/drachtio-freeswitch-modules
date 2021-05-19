include $(top_srcdir)/build/modmake.rulesam
MODNAME=mod_aws_transcribe

mod_LTLIBRARIES = mod_aws_transcribe.la
mod_aws_transcribe_la_SOURCES  = mod_aws_transcribe.c aws_transcribe_glue.cpp
mod_aws_transcribe_la_CFLAGS   = $(AM_CFLAGS)
mod_aws_transcribe_la_CXXFLAGS = $(AM_CXXFLAGS) -std=c++11 -I${switch_srcdir}/libs/aws-sdk-cpp/aws-cpp-sdk-core/include -I${switch_srcdir}/libs/aws-sdk-cpp/aws-cpp-sdk-transcribestreaming/include -I${switch_srcdir}/libs/aws-sdk-cpp/build/.deps/install/include

mod_aws_transcribe_la_LIBADD   = $(switch_builddir)/libfreeswitch.la
mod_aws_transcribe_la_LDFLAGS  = -avoid-version -module -no-undefined -L${switch_srcdir}/libs/aws-sdk-cpp/build/aws-cpp-sdk-core -L${switch_srcdir}/libs/aws-sdk-cpp/build/aws-cpp-sdk-transcribestreaming -L${switch_srcdir}/libs/aws-sdk-cpp/build/crt/aws-crt-cpp -laws-cpp-sdk-transcribestreaming -laws-cpp-sdk-core -laws-crt-cpp -lpthread -lcurl -lcrypto -lssl -lz
