include $(top_srcdir)/build/modmake.rulesam
MODNAME=mod_audio_fork

mod_LTLIBRARIES = mod_audio_fork.la
mod_audio_fork_la_SOURCES  = mod_audio_fork.c lws_glue.cpp parser.cpp audio_pipe.cpp
mod_audio_fork_la_CFLAGS   = $(AM_CFLAGS)
mod_audio_fork_la_CXXFLAGS = $(AM_CXXFLAGS) -std=c++11

mod_audio_fork_la_LIBADD   = $(switch_builddir)/libfreeswitch.la
mod_audio_fork_la_LDFLAGS  = -avoid-version -module -no-undefined -shared `pkg-config --libs libwebsockets` 
