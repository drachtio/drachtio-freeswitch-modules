include $(top_srcdir)/build/modmake.rulesam
MODNAME=mod_google_tts

mod_LTLIBRARIES = mod_google_tts.la
mod_google_tts_la_SOURCES  = mod_google_tts.c google_glue.cpp
mod_google_tts_la_CFLAGS   = $(AM_CFLAGS)
mod_google_tts_la_CXXFLAGS = -I $(top_srcdir)/libs/googleapis/gens $(AM_CXXFLAGS) -std=c++17
mod_google_tts_la_LIBADD   = $(switch_builddir)/libfreeswitch.la
mod_google_tts_la_LDFLAGS  = -avoid-version -module -no-undefined -shared `pkg-config --libs grpc++ grpc` 
