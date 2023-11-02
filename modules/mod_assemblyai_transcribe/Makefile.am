include $(top_srcdir)/build/modmake.rulesam
MODNAME=mod_assemblyai_transcribe

mod_LTLIBRARIES = mod_assemblyai_transcribe.la
mod_assemblyai_transcribe_la_SOURCES  = mod_assemblyai_transcribe.c aai_transcribe_glue.cpp audio_pipe.cpp parser.cpp
mod_assemblyai_transcribe_la_CFLAGS   = $(AM_CFLAGS)
mod_assemblyai_transcribe_la_CXXFLAGS = $(AM_CXXFLAGS) -std=c++11
mod_assemblyai_transcribe_la_LIBADD   = $(switch_builddir)/libfreeswitch.la
mod_assemblyai_transcribe_la_LDFLAGS  = -avoid-version -module -no-undefined -shared `pkg-config --libs libwebsockets` 
