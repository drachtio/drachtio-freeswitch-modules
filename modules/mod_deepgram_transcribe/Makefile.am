include $(top_srcdir)/build/modmake.rulesam
MODNAME=mod_deepgram_transcribe

mod_LTLIBRARIES = mod_deepgram_transcribe.la
mod_deepgram_transcribe_la_SOURCES  = mod_deepgram_transcribe.c dg_transcribe_glue.cpp audio_pipe.cpp parser.cpp
mod_deepgram_transcribe_la_CFLAGS   = $(AM_CFLAGS)
mod_deepgram_transcribe_la_CXXFLAGS = $(AM_CXXFLAGS) -std=c++11
mod_deepgram_transcribe_la_LIBADD   = $(switch_builddir)/libfreeswitch.la
mod_deepgram_transcribe_la_LDFLAGS  = -avoid-version -module -no-undefined -shared `pkg-config --libs libwebsockets` 
