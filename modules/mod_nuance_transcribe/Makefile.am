include $(top_srcdir)/build/modmake.rulesam
MODNAME=mod_nuance_transcribe

mod_LTLIBRARIES = mod_nuance_transcribe.la
mod_nuance_transcribe_la_SOURCES  = mod_nuance_transcribe.c nuance_glue.cpp 
mod_nuance_transcribe_la_CFLAGS   = $(AM_CFLAGS)
mod_nuance_transcribe_la_CXXFLAGS = -I $(top_srcdir)/libs/nuance-asr-grpc-api/stubs $(AM_CXXFLAGS) -std=c++17

mod_nuance_transcribe_la_LIBADD   = $(switch_builddir)/libfreeswitch.la
mod_nuance_transcribe_la_LDFLAGS  = -avoid-version -module -no-undefined -shared `pkg-config --libs grpc++ grpc`

