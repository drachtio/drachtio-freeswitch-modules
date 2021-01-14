include $(top_srcdir)/build/modmake.rulesam
MODNAME=mod_aws_lex

mod_LTLIBRARIES = mod_aws_lex.la
mod_aws_lex_la_SOURCES  = mod_aws_lex.c aws_lex_glue.cpp parser.cpp
mod_aws_lex_la_CFLAGS   = $(AM_CFLAGS)
mod_aws_lex_la_CXXFLAGS = $(AM_CXXFLAGS) -std=c++11 -I${switch_srcdir}/libs/aws-sdk-cpp/aws-cpp-sdk-core/include -I${switch_srcdir}/libs/aws-sdk-cpp/aws-cpp-sdk-lexv2-runtime/include -I${switch_srcdir}/libs/aws-sdk-cpp/build/.deps/install/include

mod_aws_lex_la_LIBADD   = $(switch_builddir)/libfreeswitch.la
mod_aws_lex_la_LDFLAGS  = -avoid-version -module -no-undefined -L${switch_srcdir}/libs/aws-sdk-cpp/build/.deps/install/lib -L${switch_srcdir}/libs/aws-sdk-cpp/build/aws-cpp-sdk-core -L${switch_srcdir}/libs/aws-sdk-cpp/build/aws-cpp-sdk-lexv2-runtime -laws-cpp-sdk-lexv2-runtime -laws-cpp-sdk-core -laws-c-event-stream -laws-checksums -laws-c-common -lpthread -lcurl -lcrypto -lssl -lz
