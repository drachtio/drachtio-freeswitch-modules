include $(top_srcdir)/build/modmake.rulesam
MODNAME=mod_nuance_transcribe

mod_LTLIBRARIES = mod_nuance_transcribe.la
mod_nuance_transcribe_la_SOURCES  = mod_nuance_transcribe.c nuance_glue.cpp
mod_nuance_transcribe_la_CFLAGS   = $(AM_CFLAGS)
mod_nuance_transcribe_la_CXXFLAGS = -I ./stubs $(AM_CXXFLAGS) -std=c++11

mod_nuance_transcribe_la_LIBADD   = $(switch_builddir)/libfreeswitch.la
mod_nuance_transcribe_la_LDFLAGS  = -avoid-version -module -no-undefined -shared `pkg-config --libs grpc++ grpc` 

BUILT_SOURCES = stubs/nuance/asr/v1/result.pb.cc stubs/nuance/asr/v1/resource.pb.cc stubs/nuance/asr/v1/recognizer.pb.cc stubs/nuance/rpc/error_details.pb.cc stubs/nuance/rpc/status_code.pb.cc stubs/nuance/rpc/status.pb.cc  

stubs/nuance/asr/v1/%.pb.cc: protos/nuance/asr/v1/%.proto
	protoc --proto_path=protos --cpp_out=stubs $^

stubs/nuance/rpc/%.pb.cc: protos/nuance/rpc/%.proto
	protoc --proto_path=protos --cpp_out=stubs $^

