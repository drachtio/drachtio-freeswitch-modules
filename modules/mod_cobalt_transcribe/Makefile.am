include $(top_srcdir)/build/modmake.rulesam
MODNAME=mod_cobalt_transcribe

mod_LTLIBRARIES = mod_cobalt_transcribe.la
mod_cobalt_transcribe_la_SOURCES  = mod_cobalt_transcribe.c cobalt_glue.cpp 
mod_cobalt_transcribe_la_CFLAGS   = $(AM_CFLAGS)
mod_cobalt_transcribe_la_CXXFLAGS =  -I $(top_srcdir)/libs/googleapis/gens -I $(top_srcdir)/libs/cobalt-asr-grpc-api/stubs $(AM_CXXFLAGS) -std=c++17

mod_cobalt_transcribe_la_LIBADD   = $(switch_builddir)/libfreeswitch.la
mod_cobalt_transcribe_la_LDFLAGS  = -avoid-version -module -no-undefined -shared `pkg-config --libs grpc++ grpc`

