#include <stdio.h>
#include <curl/curl.h>
#include <jsoncpp/json/json.h>
#include <stdlib.h>
#include <memory.h>
#include <sstream>

using std::stringstream;
using std::ostream;

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

int main(void)
{
    CURL *curl;
    CURLcode res;
   
//    static const char *postthis = "{ \"size\": 5, \"operators\": \"fold,tfold\" }";
    static const char *postthis = "";

    curl = curl_easy_init();
    if (!curl)
        return 1;
   
    stringstream stream;

//    curl_easy_setopt(curl, CURLOPT_URL, "http://icfpc2013.cloudapp.net/train?auth=0451EUqILPkx1zWe7fD4BMiNzwHIjPGCkbKYFxI0vpsH1H");
    curl_easy_setopt(curl, CURLOPT_URL, "http://icfpc2013.cloudapp.net/myproblems?auth=0451EUqILPkx1zWe7fD4BMiNzwHIjPGCkbKYFxI0vpsH1H");    
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postthis);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, response);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &stream);

    // Perform the request, res will get the return code 
    res = curl_easy_perform(curl);

    if (res != CURLE_OK) {
        fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
    }
   
    Json::Reader reader;
    Json::Value root;
    if (!reader.parse(stream, root)) {
        fprintf(stderr, "Failed to parse Json");
        exit(1);
    }

    int sizes[31];
    memset(sizes, 0, sizeof(sizes));

    for (int i = 0; i < root.size(); i++) {
        const Json::Value& item = root[i];
        unsigned size = item["size"].asUInt();
        bool solved = item["solved"].asBool();
        printf("%4i: %s %3u %10s\n", i, item["id"].asCString(), size, solved ? "solved" : "");
        if (size <= 30)
            ++sizes[size];
    }

    for (int i = 0; i <= 30; i++)
        printf("size %2d: %3d items\n", i, sizes[i]);

    curl_easy_cleanup(curl);

    return 0;
}