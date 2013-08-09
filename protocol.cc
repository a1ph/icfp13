#include <stdio.h>
#include <curl/curl.h>
#include <jsoncpp/json/json.h>
#include <stdlib.h>
#include <stdint.h>
#include <memory.h>
#include <sstream>

using std::stringstream;
using std::ostream;
using std::string;

typedef uint64_t Val; // todo remove

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

    printf("got train task:\n%s\n", result.toStyledString().c_str());

    challenge(result["id"].asCString(), result["size"].asInt(), result["operators"]);
}

void Protocol::challenge(const string& id, int size, const Json::Value& operators)
{
    printf("Challenge:\tid: %s size: %d operators: %s\n", id.c_str(), size, operators.toStyledString().c_str());
    
    Json::Value args;

    Val inp[] = { 0xB445FBB8CDDCF9F8, 0xEFE7EA693DD952DE, 0x6D326AEEB275CF14, 0xBB5F96D91F43B9F3,
                  0xF246BDD3CFDEE59E, 0x28E6839E4B1EEBC1, 0x9273A5C811B2217B, 0xA841129BBAB18B3E,
                  0x0, 0x1, 0xaa5555aa5555aaaa, 0xff00000000000000, 0x00ff000000000000 };

    for (int i = 0; i < sizeof(inp) / sizeof(*inp); i++) {
        char buffer[100];
        snprintf(buffer, sizeof(buffer), "0x%lx", inp[i]);
        args[i] = buffer;
    }

    Json::Value request;
    Json::Value response;
    request["id"] = id;
    request["arguments"] = args;
    printf("req: %s\n", request.toStyledString().c_str());
    send("eval", request, response);
    printf("res: %s\n", response.toStyledString().c_str());
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

//    p.print_tasks();
    p.train(3);

    return 0;
}

