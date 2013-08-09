#include "generator.h"

#include <stdio.h>
#include <curl/curl.h>
#include <jsoncpp/json/json.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include <memory.h>
#include <sstream>

using std::stringstream;
using std::ostream;
using std::string;

uint64_t timestamp()
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0;
    return ts.tv_sec * 1000ul + ts.tv_nsec / 1000000;
}

class Protocol
{
public:
    Protocol();
    ~Protocol();

    void train(int size);

    bool challenge(const string& id, int size, const Json::Value& operators);

    void print_tasks();
    void solve_my_tasks(int up_to_size);

    void guess(const string& id, const string &program, Json::Value& result);

private:
    bool send(const char* command, const Json::Value& request, Json::Value& result);

    void get_data(const char* data, size_t len);
    void retrieve_my_tasks();

    static size_t response(void *ptr, size_t size, size_t nmemb, void *user_data);

    CURL *curl;
    ostream* stream_;
    uint64_t started_;
    Json::Value my_tasks_;
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

class Solver : public Verifier
{
public:
    Solver(const string& id, Protocol* protocol) : id_(id), protocol_(protocol), win_(false) {}
    virtual bool action(Expr* program);

    Protocol* protocol_;
    string id_;
    bool win_;
};

bool Solver::action(Expr* program)
{
    for (Pairs::iterator it = pairs.begin(); it != pairs.end(); ++it) {
        Val actual = program->run((*it).first);
        if (actual != (*it).second)
            return true;
    }
    printf("%6d: %s\n", ++count, program->program().c_str());

    Json::Value result;
    protocol_->guess(id_, program->program(), result);

    if (result["status"] == "win") {
        win_ = true;
        return false;
    }

    if (result["status"] == "mismatch") {
        Json::Value values = result["values"];
        Val inp, out;
        sscanf(values[0].asCString(), "%lx", &inp);
        sscanf(values[1].asCString(), "%lx", &out);
        printf("parsed 0x%lx 0x%lx\n", inp, out);
        add(inp, out);
        return true;
    }

    return false;
}

bool Protocol::challenge(const string& id, int size, const Json::Value& operators)
{
    started_ = timestamp();
    printf("Challenge ACCEPTED:\nid: %s\nsize: %d\noperators: %s", id.c_str(), size, operators.toStyledString().c_str());

    Val inp[] = { 0xB445FBB8CDDCF9F8, 0xEFE7EA693DD952DE, 0x6D326AEEB275CF14, 0xBB5F96D91F43B9F3,
                  0xF246BDD3CFDEE59E, 0x28E6839E4B1EEBC1, 0x9273A5C811B2217B, 0xA841129BBAB18B3E,
                  0x0, 0x1, 0xaa5555aa5555aaaa,
                  0xff00000000000000,
                  0x00ff000000000000,
                  0x0000ff0000000000,
                  0x000000ff00000000,
                  0x00000000ff000000,
                  0x0000000000ff0000,
                  0x000000000000ff00,
                  0x00000000000000ff,
    };

    Json::Value inputs;
    for (int i = 0; i < sizeof(inp) / sizeof(*inp); i++) {
        char buffer[100];
        snprintf(buffer, sizeof(buffer), "0x%lx", inp[i]);
        inputs[i] = buffer;
    }

    Json::Value request;
    Json::Value response;
    request["id"] = id;
    request["arguments"] = inputs;
    printf("req: %s\n", request.toStyledString().c_str());
    send("eval", request, response);
    printf("res: %s\n", response.toStyledString().c_str());

    if (response["status"].asString() != "ok") {
        fprintf(stderr, "an error!!!\n");
        exit(1);
    }
    Generator g;
    Solver solver(id, this);

    Json::Value outputs = response["outputs"];
    for (int i = 0; i < sizeof(inp) / sizeof(*inp); i++) {
        Val out;
        sscanf(outputs[i].asCString(), "%lx", &out);
        solver.add(inp[i], out);
    }
    g.allow_all();
    printf("start generation at %lu ms\n", timestamp() - started_);
    g.generate(size, &solver);

    printf("CHALLENGE done in %lu ms\n", timestamp() - started_);
    return solver.win_;
}

void Protocol::guess(const string& id, const string &program, Json::Value& result)
{
    printf("guess initiated at %lu ms\n", timestamp() - started_);
    Json::Value request;
    request["id"] = id;
    request["program"] = program;

    if (!send("guess", request, result)) {
        fprintf(stderr, "failed guess\n");
        exit(1);
    }

    printf("guess done at %lu ms\n", timestamp() - started_);
    printf("guess response:\n%s\n", result.toStyledString().c_str());
}

void Protocol::print_tasks()
{
    retrieve_my_tasks();

    int sizes[31];
    int wins[31];
    memset(sizes, 0, sizeof(sizes));
    memset(wins, 0, sizeof(wins));

    for (int i = 0; i < my_tasks_.size(); i++) {
        const Json::Value& item = my_tasks_[i];
        unsigned size = item["size"].asUInt();
        bool solved = item["solved"].asBool();
        int time_left = item.get("timeLeft", Json::Value(-1)).asInt();
        printf("%4i: %s %3u %10s %10d\n", i, item["id"].asCString(), size, solved ? "SOLVED" : "", time_left);
        if (size <= 30) {
            ++sizes[size];
            if (solved)
                ++wins[size];
        }
    }

    for (int i = 0; i <= 30; i++)
        printf("size %2d: %2d / %2d\n", i, wins[i], sizes[i]);
}

void Protocol::retrieve_my_tasks()
{
    Json::Value request;
    if (!send("myproblems", request, my_tasks_)) {
        fprintf(stderr, "Requst failed\n");
        return;
    }
}

void Protocol::solve_my_tasks(int up_to_size)
{
    retrieve_my_tasks();

    for (int size = up_to_size; size <= up_to_size; size++) {
        // find an unsolved task of appropriate size.
        for (int i = 0; i < my_tasks_.size(); i++) {
            Json::Value& item = my_tasks_[i];
            if (item["size"].asInt() != size)
                continue;
            if (item["solved"].asBool())
                continue;

            bool win = challenge(item["id"].asString(), item["size"].asInt(), item["operators"]);
            if (win)
                item["solved"] = true;
        }
    }
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
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30);

    // Perform the request, res will get the return code 
    CURLcode res = curl_easy_perform(curl);

    if (res != CURLE_OK) {
        fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
        return false;
    }
   
    Json::Reader reader;
    if (!reader.parse(stream, result)) {
        fprintf(stderr, "Failed to parse Json\n");
        printf("sleeping for 5 sec...\n");
        sleep(5);
        return send(command, request, result);
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

int main(int argc, char* argv[])
{
    Protocol p;

    if (argc < 2)
        return 1;

    string arg = argv[1];
    if (arg == "print")
        p.print_tasks();
    else if (arg == "solve_my" && argc > 2)
        p.solve_my_tasks(atoi(argv[2]));
    else if (argc > 2) {
        Json::Value allowed;
        allowed[0] = "and";
        allowed[1] = "not";
        p.challenge(argv[1], atoi(argv[2]), allowed);

    } else {
        p.train(10);
    }

    return 0;
}

