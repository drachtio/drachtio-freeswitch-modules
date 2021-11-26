include $(top_srcdir)/build/modmake.rulesam
MODNAME=mod_azure_transcribe

mod_LTLIBRARIES = mod_azure_transcribe.la
mod_azure_transcribe_la_SOURCES  = mod_azure_transcribe.c azure_transcribe_glue.cpp
mod_azure_transcribe_la_CFLAGS   = $(AM_CFLAGS)
mod_azure_transcribe_la_CXXFLAGS = $(AM_CXXFLAGS) -std=c++14 -I/usr/local/include/MicrosoftSpeechSDK/cxx_api -I/usr/local/include/MicrosoftSpeechSDK/c_api

mod_azure_transcribe_la_LIBADD   = $(switch_builddir)/libfreeswitch.la
mod_azure_transcribe_la_LDFLAGS  = -avoid-version -module -no-undefined -L/usr/local/lib/MicrosoftSpeechSDK/x64 -lMicrosoft.CognitiveServices.Speech.core -l:libasound.so.2 -lpthread -lcrypto -lssl -lz