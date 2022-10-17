include $(top_srcdir)/build/modmake.rulesam
MODNAME=mod_nuance_transcribe

mod_LTLIBRARIES = mod_nuance_transcribe.la
mod_nuance_transcribe_la_SOURCES  = \
	stubs/nuance/rpc/error_details.pb.cc stubs/nuance/rpc/status_code.pb.cc stubs/nuance/rpc/status.pb.cc \
	stubs/nuance/rpc/error_details.grpc.pb.cc stubs/nuance/rpc/status_code.grpc.pb.cc stubs/nuance/rpc/status.grpc.pb.cc \
	stubs/nuance/asr/v1/result.pb.cc stubs/nuance/asr/v1/resource.pb.cc stubs/nuance/asr/v1/recognizer.pb.cc \
	stubs/nuance/asr/v1/result.grpc.pb.cc stubs/nuance/asr/v1/resource.grpc.pb.cc stubs/nuance/asr/v1/recognizer.grpc.pb.cc \
	mod_nuance_transcribe.c nuance_glue.cpp 
mod_nuance_transcribe_la_CFLAGS   = $(AM_CFLAGS)
mod_nuance_transcribe_la_CXXFLAGS = -I ./stubs $(AM_CXXFLAGS) -std=c++11

mod_nuance_transcribe_la_LIBADD   = $(switch_builddir)/libfreeswitch.la
mod_nuance_transcribe_la_LDFLAGS  = -avoid-version -module -no-undefined -shared `pkg-config --libs grpc++ grpc` -lprotobuf -lpthread -ldl

BUILT_SOURCES = stubs/nuance/rpc/error_details.pb.h

stubs/nuance/asr/v1/%.pb.cc stubs/nuance/asr/v1/%.pb.h: protos/nuance/asr/v1/%.proto
	mkdir -p stubs
	protoc --proto_path=./protos:/usr/local/include/ --cpp_out=stubs --grpc_out=stubs --plugin=protoc-gen-grpc=`which grpc_cpp_plugin` $^

stubs/nuance/rpc/%.pb.cc stubs/nuance/rpc/%.pb.h: protos/nuance/rpc/%.proto
	mkdir -p stubs
	protoc --proto_path=./protos:/usr/local/include/ --cpp_out=./stubs --grpc_out=stubs --plugin=protoc-gen-grpc=`which grpc_cpp_plugin` $^

