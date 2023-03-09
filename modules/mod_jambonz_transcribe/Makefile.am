include $(top_srcdir)/build/modmake.rulesam
MODNAME=mod_jambonz_transcribe

mod_LTLIBRARIES = mod_jambonz_transcribe.la
mod_jambonz_transcribe_la_SOURCES  = mod_jambonz_transcribe.c jb_transcribe_glue.cpp audio_pipe.cpp parser.cpp
mod_jambonz_transcribe_la_CFLAGS   = $(AM_CFLAGS)
mod_jambonz_transcribe_la_CXXFLAGS = $(AM_CXXFLAGS) -std=c++11
mod_jambonz_transcribe_la_LIBADD   = $(switch_builddir)/libfreeswitch.la
mod_jambonz_transcribe_la_LDFLAGS  = -avoid-version -module -no-undefined -shared `pkg-config --libs libwebsockets` 
