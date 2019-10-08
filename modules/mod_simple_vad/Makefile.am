include $(top_srcdir)/build/modmake.rulesam
MODNAME=mod_simple_vad

mod_LTLIBRARIES = mod_simple_vad.la
mod_simple_vad_la_SOURCES  = mod_simple_vad.c
mod_simple_vad_la_CFLAGS   = $(AM_CFLAGS)
mod_simple_vad_la_LIBADD   = $(switch_builddir)/libfreeswitch.la
mod_simple_vad_la_LDFLAGS  = -avoid-version -module -no-undefined -shared
