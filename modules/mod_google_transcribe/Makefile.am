include $(top_srcdir)/build/modmake.rulesam
MODNAME=mod_google_transcribe

mod_LTLIBRARIES = mod_google_transcribe.la
mod_google_transcribe_la_SOURCES  = mod_google_transcribe.c google_glue.cpp
mod_google_transcribe_la_CFLAGS   = $(AM_CFLAGS)
mod_google_transcribe_la_CXXFLAGS = -I $(top_srcdir)/libs/googleapis/gens $(AM_CXXFLAGS) -std=c++11

mod_google_transcribe_la_LIBADD   = $(switch_builddir)/libfreeswitch.la
mod_google_transcribe_la_LDFLAGS  = -avoid-version -module -no-undefined -shared `pkg-config --libs grpc++ grpc` 
