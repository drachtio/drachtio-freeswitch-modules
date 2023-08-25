include $(top_srcdir)/build/modmake.rulesam
MODNAME=mod_soniox_transcribe

mod_LTLIBRARIES = mod_soniox_transcribe.la
mod_soniox_transcribe_la_SOURCES  = mod_soniox_transcribe.c soniox_glue.cpp 
mod_soniox_transcribe_la_CFLAGS   = $(AM_CFLAGS)
mod_soniox_transcribe_la_CXXFLAGS = -I $(top_srcdir)/libs/soniox-asr-grpc-api/stubs $(AM_CXXFLAGS) -std=c++17

mod_soniox_transcribe_la_LIBADD   = $(switch_builddir)/libfreeswitch.la
mod_soniox_transcribe_la_LDFLAGS  = -avoid-version -module -no-undefined -shared `pkg-config --libs grpc++ grpc`

