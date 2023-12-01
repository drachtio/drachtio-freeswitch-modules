#ifndef TRANSCRIBEMANAGER_HPP_
#define TRANSCRIBEMANAGER_HPP_

#include <string>
#include <vector>

/** Usage
 #include "transcribe_manager.hpp"

 // get signed URL
 const string url = TranscribeManager::getSignedWebsocketUrl(accessKey_, secretKey_, region_);

 // connect to the url using a socket library (e.g. https://github.com/machinezone/IXWebSocket)

 // build request string
 string request;
 TranscribeManager::makeRequest(request, audioData); // audioData is a const vector<uint8_t>

 // send request to socket
 * 
 */

using namespace std;

class TranscribeManager {
public:
    static void getSignedWebsocketUrl(string& host, string& path,
            const std::string& accessKey, const std::string& secretKey, const std::string& securityToken, 
            const std::string& region, const std::string& lang, const char* vocabularyName,
            const char* vocabularyFilterName, const char* vocabularyFilterMethod);

    static bool parseResponse(const std::string& response, std::string& payload, bool& isError, bool verbose = false);

    static bool makeRequest(std::string& request, const std::vector<uint8_t>& data);
    static void writeHeader(char** buffer, const char* key, const char* val);

private:
    static std::string getSha256(std::string str);
    static void getSignatureKey(unsigned char *signatureKey, const std::string& secretKey,
            const std::string& datestamp, const std::string& region, const std::string& service);
    static void getHMAC(unsigned char *hmac, unsigned char *key, int keyLen, const std::string& str);
    static std::string toHex(unsigned char *hmac);

    static bool verifyCRC(const char* buffer, const uint32_t totalLength);
    static void parseHeader(const char** buffer, bool& isError, bool verbose = false);

};

#endif /* TRANSCRIBEMANAGER_HPP_ */
