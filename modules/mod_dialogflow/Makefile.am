include $(top_srcdir)/build/modmake.rulesam
MODNAME=mod_dialogflow

mod_LTLIBRARIES = mod_dialogflow.la
mod_dialogflow_la_SOURCES  = mod_dialogflow.c google_glue.cpp parser.cpp
mod_dialogflow_la_CFLAGS   = $(AM_CFLAGS)
mod_dialogflow_la_CXXFLAGS = -I $(top_srcdir)/libs/googleapis/gens $(AM_CXXFLAGS) -std=c++11

mod_dialogflow_la_LIBADD   = $(switch_builddir)/libfreeswitch.la
mod_dialogflow_la_LDFLAGS  = -avoid-version -module -no-undefined -shared `pkg-config --libs grpc++ grpc` 
