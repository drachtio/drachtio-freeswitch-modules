include $(top_srcdir)/build/modmake.rulesam
MODNAME=mod_ibm_transcribe

mod_LTLIBRARIES = mod_ibm_transcribe.la
mod_ibm_transcribe_la_SOURCES  = mod_ibm_transcribe.c ibm_transcribe_glue.cpp audio_pipe.cpp parser.cpp
mod_ibm_transcribe_la_CFLAGS   = $(AM_CFLAGS)
mod_ibm_transcribe_la_CXXFLAGS = $(AM_CXXFLAGS) -std=c++11
mod_ibm_transcribe_la_LIBADD   = $(switch_builddir)/libfreeswitch.la
mod_ibm_transcribe_la_LDFLAGS  = -avoid-version -module -no-undefined -shared `pkg-config --libs libwebsockets` 
