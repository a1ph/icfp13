#include <stdio.h>
#include <curl/curl.h>
#include <jsoncpp/json/json.h>
#include <stdlib.h>
#include <memory.h>
#include <sstream>

using std::stringstream;
using std::ostream;
using std::string;

size_t response(void *ptr, size_t size, size_t nmemb, void *user_data)
{
    Json::Reader reader;
    Json::Value root;

    const char* buffer = (const char*)ptr;
    size_t len = size * nmemb;

    ostream* stream = (stringstream*)user_data;
    stream->write(buffer, len);

    return len;
}

class Protocol
{
public:
    Protocol();
    ~Protocol();

    void train(int size);

    void challenge(const string& id, int size, const Json::Value& operators);

    void print_tasks();

private:
    bool send(const char* command, const Json::Value& request, Json::Value& result);

    void get_data(const char* data, size_t len);

    static size_t response(void *ptr, size_t size, size_t nmemb, void *user_data);

    CURL *curl;
    ostream* stream_;
};

Protocol::Protocol()
{
    curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "Curl initialization failed\n");
        exit(1);
    }
}

Protocol::~Protocol()
{
    curl_easy_cleanup(curl);
}

void Protocol::train(int size)
{
    Json::Value request;
    request["size"] = size;

    Json::Value result;
    if (!send("train", request, result)) {
        fprintf(stderr, "Task aquisition failed\n");
        return;
    }

    printf("got train task: %s\n", result.toStyledString().c_str());
}

void Protocol::print_tasks()
{
    Json::Value request;
    Json::Value root; // todo obtain it.
    if (!send("myproblems", request, root)) {
        fprintf(stderr, "Requst failed\n");
        return;
    }

    int sizes[31];
    memset(sizes, 0, sizeof(sizes));

    for (int i = 0; i < root.size(); i++) {
        const Json::Value& item = root[i];
        unsigned size = item["size"].asUInt();
        bool solved = item["solved"].asBool();
        printf("%4i: %s %3u %10s\n", i, item["id"].asCString(), size, solved ? "SOLVED" : "");
        if (size <= 30)
            ++sizes[size];
    }

    for (int i = 0; i <= 30; i++)
        printf("size %2d: %3d items\n", i, sizes[i]);
}

bool Protocol::send(const char* command, const Json::Value& request, Json::Value& result)
{
    char url[1000];
    snprintf(url, 1000, "http://icfpc2013.cloudapp.net/%s?auth=0451EUqILPkx1zWe7fD4BMiNzwHIjPGCkbKYFxI0vpsH1H", command);

    string data_string = request.toStyledString();

    stringstream stream;
    stream_ = &stream;

    curl_easy_setopt(curl, CURLOPT_URL, url);    
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data_string.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, response);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, this);

    // Perform the request, res will get the return code 
    CURLcode res = curl_easy_perform(curl);

    if (res != CURLE_OK) {
        fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
        return false;
    }
   
    Json::Reader reader;
    if (!reader.parse(stream, result)) {
        fprintf(stderr, "Failed to parse Json");
        return false;
    }

    return true;
}

size_t Protocol::response(void *ptr, size_t size, size_t nmemb, void *user_data)
{
    Json::Reader reader;
    Json::Value root;

    const char* buffer = (const char*)ptr;
    size_t len = size * nmemb;

    Protocol* p = (Protocol*)user_data;
    p->get_data(buffer, len);

    return len;
}

void Protocol::get_data(const char* data, size_t len)
{
    stream_->write(data, len);
}

int main(void)
{
    Protocol p;

    p.print_tasks();
    p.train(3);

    return 0;
}

