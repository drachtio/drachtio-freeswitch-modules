#include <cstdlib>
#include <algorithm>
#include <future>

#include <switch.h>
#include <switch_json.h>
#include <grpc++/grpc++.h>
#include <grpcpp/impl/codegen/sync_stream.h>

#include "mod_google_transcribe.h"
#include "simple_buffer.h"

#define CHUNKSIZE (320)

namespace {
  int case_insensitive_match(std::string s1, std::string s2) {
   std::transform(s1.begin(), s1.end(), s1.begin(), ::tolower);
   std::transform(s2.begin(), s2.end(), s2.begin(), ::tolower);
   if(s1.compare(s2) == 0)
      return 1; //The strings are same
   return 0; //not matched
  }
}

template <typename Request, typename Response, typename Stub>
class GStreamer {
public:
	GStreamer(
    switch_core_session_t *session, 
    uint32_t channels, 
    char* lang, 
    int interim, 
    uint32_t config_sample_rate,
		uint32_t samples_per_second,
    int single_utterance, 
    int separate_recognition,
		int max_alternatives, 
    int profanity_filter, 
    int word_time_offset, 
    int punctuation, 
    const char* model, 
    int enhanced, 
	const char* hints);

	~GStreamer() {
		//switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_INFO, "GStreamer::~GStreamer - deleting channel and stub: %p\n", (void*)this);
	}

	bool write(void* data, uint32_t datalen);

    void connect();
    
	uint32_t nextMessageSize(void) {
		uint32_t size = 0;
		m_streamer->NextMessageSize(&size);
		return size;
	}

	bool read(Response* response) {
		return m_streamer->Read(response);
	}

	grpc::Status finish() {
		return m_streamer->Finish();
	}

	void writesDone() {
		// grpc crashes if we call this twice on a stream
		if (!m_connected) {
			cancelConnect();
		}
		else if (!m_writesDone) {
			m_streamer->WritesDone();
			m_writesDone = true;
		}
	}

	bool waitForConnect() {
		std::shared_future<void> sf(m_promise.get_future());
		sf.wait();
		return m_connected;
	}

	void cancelConnect() {
		assert(!m_connected);
		m_promise.set_value();
	}

	bool isConnected() {
		return m_connected;
	}

private:
	std::shared_ptr<grpc::Channel> create_grpc_channel(switch_channel_t *channel) {
	    const char* google_uri;
		if (!(google_uri = switch_channel_get_variable(channel, "GOOGLE_SPEECH_TO_TEXT_URI"))) {
			google_uri = "speech.googleapis.com";
		}

	    const char* var;
		if (var = switch_channel_get_variable(channel, "GOOGLE_APPLICATION_CREDENTIALS")) {
			auto channelCreds = grpc::SslCredentials(grpc::SslCredentialsOptions());
			auto callCreds = grpc::ServiceAccountJWTAccessCredentials(var);
			auto creds = grpc::CompositeChannelCredentials(channelCreds, callCreds);
			return grpc::CreateChannel(google_uri, creds);
		}
		else {
			auto creds = grpc::GoogleDefaultCredentials();
			return grpc::CreateChannel(google_uri, creds);
		}
	}

	switch_core_session_t* m_session;
	grpc::ClientContext m_context;
	std::shared_ptr<grpc::Channel> m_channel;
	std::unique_ptr<Stub> 	m_stub;
	std::unique_ptr< grpc::ClientReaderWriterInterface<Request, Response> > m_streamer;
	Request m_request;
	bool m_writesDone;
	bool m_connected;
	std::promise<void> m_promise;
	SimpleBuffer m_audioBuffer;
};
