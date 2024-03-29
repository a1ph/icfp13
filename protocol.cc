#define __STDC_FORMAT_MACROS

#include "gen2.h"
#include "analyzer.h"

#include <inttypes.h>
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

long timestamp()
{
    struct timeval tv;
    if (gettimeofday(&tv, NULL) < 0)
        return 0;
    return (static_cast<long>(tv.tv_sec) * 1000) + tv.tv_usec / 1000;
#if 0
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0;
    return ts.tv_sec * 1000ul + ts.tv_nsec / 1000000;
#endif
}

long started_;

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
    request["operators"] = "fold";//Json::Value(Json::arrayValue);

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
    virtual bool action(Expr* program, int size);

    Protocol* protocol_;
    string id_;
    bool win_;
    long cnt;
};

bool Solver::action(Expr* program, int size)
{
    cnt++;
    if ((cnt & 0x7fffff) == 0) {
        long ts = timestamp();
        long elapsed = ts - started_;
        printf("??? %6lu ms  %9lu: [%d] %s     \n",
            elapsed, cnt, size, program->program().c_str());
        fflush(stdout);
        if (elapsed > 320 * 1000) {
            printf("\n ===================== TIME IS OUT :-(( ========================\n\n");
            return false;
        }
    }
    for (Pairs::iterator it = pairs.begin(); it != pairs.end(); ++it) {
        Val actual = program->run((*it).first);
        if (actual != (*it).second)
            return true;
    }
    printf("\n!!! %6lu: [%d] %s    \n",
        cnt, size, program->program().c_str());

    Json::Value result;
    protocol_->guess(id_, program->program(), result);

    if (result["status"] == "win") {
        win_ = true;
        return false;
    }

    if (result["status"] == "mismatch") {
        Json::Value values = result["values"];
        Val inp, out;
        sscanf(values[0u].asCString(), "%"PRIx64, &inp);
        sscanf(values[1u].asCString(), "%"PRIx64, &out);
        printf("parsed 0x%"PRIx64" 0x%"PRIx64"\n", inp, out);
        add(inp, out);
        return true;
    }

    return false;
}

bool Protocol::challenge(const string& id, int size, const Json::Value& operators)
{
    started_ = timestamp();
    printf("Challenge ACCEPTED:\nid: %s\nsize: %d\noperators: %s", id.c_str(), size, operators.toStyledString().c_str());

    Val inp[256];
    int inp_size = 0;

    Val inp1[] = { 0xB445FBB8CDDCF9F8, 0xEFE7EA693DD952DE, 0x6D326AEEB275CF14, 0xBB5F96D91F43B9F3,
                   0x0, 0x1, 2, 3, 4, 5,
                   0xaa5555aa5555aaaa,
                   0xcc660330660f0cc0,
                   6, 7, 10, 11, 15, 16, 53, 63, 120, 183, 246,
                   0x0000000000000001,
                   0x0000000000000200,
                   0x0000000000040000,
                   0x0000000008000000,
                   0x0000001000000000,
                   0x0000200000000000,
                   0x0040000000000000,
                   0x8000000000000000,
                   0x0001001300000000,
                   0xFFFEFFECFFFFFFFF
    };

    inp[inp_size++] = 0xB445FBB8CDDCF9F8;
    inp[inp_size++] = 0x0f0f0f0f0f0f0000;
    inp[inp_size++] = 0x0f0f0f0f0f1f0000;
    inp[inp_size++] = 0x0f0f0f0f1f0f0000;
    inp[inp_size++] = 0x0f0f0f1f0f0f0000;
    inp[inp_size++] = 0x0f0f1f0f0f0f0000;
    inp[inp_size++] = 0x0f1f0f0f0f0f0000;
    inp[inp_size++] = 0x1f0f0f0f0f0f0000;
    inp[inp_size++] = 0;
    inp[inp_size++] = -1;
/*    for (int i = 63; i > 0; i--)
        inp[inp_size++] = (1ul << i) - 1;
    for (int i = 1; i < 64; i++)
        inp[inp_size++] = (1ul << i);
*/    for (int i = 0; i < 8; i++)
        inp[inp_size++] = 0xfful << (i*8);

    for (int i = 0; i < sizeof(inp1) / sizeof(*inp1); i++)
        inp[inp_size++] = inp1[i];

    Json::Value inputs(Json::arrayValue);
    for (int i = 0; i < inp_size; i++) {
        char buffer[100];
        snprintf(buffer, sizeof(buffer), "0x%"PRIx64, inp[i]);
        inputs[i] = buffer;
    }

    Json::Value request;
    Json::Value response;
    request["id"] = id;
    request["arguments"] = inputs;
//    printf("req: %s\n", request.toStyledString().c_str());
    send("eval", request, response);
//    printf("res: %s\n", response.toStyledString().c_str());

    if (response["status"].asString() != "ok") {
        fprintf(stderr, "an error!!!\n");
        exit(1);
    }
    Generator g;
    Solver solver(id, this);
    Analyzer a;
    solver.cnt = 0;

    int properties = 0;
    Json::Value outputs = response["outputs"];
    for (int i = 0; i < inp_size; i++) {
        Val out;
        Val in = inp[i];
        sscanf(outputs[i].asCString(), "%"PRIx64, &out);
        solver.add(in, out);
        int d = a.distance(in, out);
//        printf("  0x%016"PRIx64" -> 0x%016"PRIx64" : dist=%2d   0x%016"PRIx64"\n", in, out, d, in^out);
        if (out & 1)   properties |= NO_TOP_SHL1;
        if (out >> 63) properties |= NO_TOP_SHR1;
        if (out >> 60) properties |= NO_TOP_SHR4;
        if (out >> 48) properties |= NO_TOP_SHR16;
        printf("  ");
        for (int i = 0; i < 64; i++) {
            printf("%d", in >= (1ul<<63));
            in <<= 1;
        }
        printf(" -> ");
        for (int i = 0; i < 64; i++) {
            printf("%d", out >= (1ul<<63));
            out <<= 1;
        }
        printf("\n");
    }
    printf("properties = 0x%x\n", properties);
    g.set_properties(properties);

    for (int i = 0; i < operators.size(); i++) {
        string ops = operators[i].asString();
        Op op;
        if (ops == "tfold") {
            g.mode_tfold_ = true;
            continue;
        }
        else if (ops == "xor") op = XOR;
        else if (ops == "and") op = AND;
        else if (ops == "plus") op = PLUS;
        else if (ops == "or") op = OR;
        else if (ops == "not") op = NOT;
        else if (ops == "shl1") op = SHL1;
        else if (ops == "shr1") op = SHR1;
        else if (ops == "shr4") op = SHR4;
        else if (ops == "shr16") op = SHR16;
        else if (ops == "fold") op = FOLD;
        else if (ops == "if0") op = IF0;
        else if (ops == "bonus") {
            g.mode_bonus_ = true;
            continue;
         //   g.allow_all();
         //   break;
        } else {
            fprintf(stderr, "Unknow op %s in allowed ops... allowing all\n", ops.c_str());
            exit(1);
        }
        g.add_allowed_op(op);
    }
    //g.add_allowed_op(NOT);
    printf("start generation at %lu ms\n", timestamp() - started_);
    g.set_callback(&solver);
    g.generate(size);

    printf("\t\t\t\t\t\t\tCHALLENGE done in %lu ms   %f ops/ms\n\n", timestamp() - started_, 1. * solver.cnt / (timestamp() - started_));

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

    int sizes[1000];
    int wins[1000];
    int lost[1000];
    memset(sizes, 0, sizeof(sizes));
    memset(wins, 0, sizeof(wins));
    memset(lost, 0, sizeof(lost));

    for (int i = 0; i < my_tasks_.size(); i++) {
        const Json::Value& item = my_tasks_[i];
        unsigned size = item["size"].asUInt();
        bool solved = item["solved"].asBool();
        int time_left = item.get("timeLeft", Json::Value(-1)).asInt();
        if (time_left == 0 && !solved)
            lost[size]++;
        string tl_str;
        char buffer[30];
        snprintf(buffer, sizeof(buffer), "%2d", time_left);
        if (time_left >= 0)
            tl_str = buffer;
        Json::Value operators = item["operators"];
        string ops_str;
        for (int j = 0; j < operators.size(); j++)
            ops_str += " " + operators[j].asString();
        printf("%4i: %s %3u %10s %10s  [%2d]:%s\n", i, item["id"].asCString(), size,
            solved ? "SOLVED" : "", tl_str.c_str(),
            operators.size(), ops_str.c_str());
        if (size < 1000) {
            ++sizes[size];
            if (solved)
                ++wins[size];
        }
    }
    printf("\n");

    int total_wins = 0;
    int total = 0;
    for (int i = 3; i < 1000; i++) {
        if (sizes[i] == 0)
            continue;
        printf("size %2d: %3d / %3d  (%d)\n", i, wins[i], sizes[i], lost[i]);
        total_wins += wins[i];
        total += sizes[i];
    }

    printf("\nTotal: %d / %d   %.1f%%\n", total_wins, total, 100. * total_wins / total);
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

    int count = 0;
    for (int size = up_to_size; size <= up_to_size; size++) {
        // find an unsolved task of appropriate size.
        for (int i = 0; i < my_tasks_.size(); i++) {
            Json::Value& item = my_tasks_[i];
            if (item["size"].asInt() != size)
                continue;
            if (item["solved"].asBool())
                continue;
            if (item["timeLeft"].isNumeric() && item["timeLeft"].asInt() == 0)
                continue;
  //          if (item["operators"].size() > 7)
  //              continue;
            bool has_tfold = false;
            for (int j = 0; j < item["operators"].size(); j++) {
                if (item["operators"][j].asString() == "fold") {
                    has_tfold = true;
                    break;
                }
            }
 //           if (has_tfold)
 //               continue;

            if (count != 0) {
                // make sure we start a new challenge in a virgin timeslot.
                int sleep_time = 5;
                printf("sleeping for %d sec", sleep_time);
                for (int i = 0; i < sleep_time; i++) {
                    sleep(1);
                    printf(".");
                    fflush(stdout);
                }
                printf("\n");
            }

            printf("\n################################ %d #################################\n", ++count);

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
    curl_easy_setopt(curl, CURLOPT_PROXY, "");

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
    else if (arg == "train" && argc > 2)
        p.train(atoi(argv[2]));
    else if (arg == "chal" && argc > 3) {
        Json::Value allowed;
        allowed[0u] = "and";
        allowed[1u] = "not";
        p.challenge(argv[2], atoi(argv[3]), allowed);
    }

    return 0;
}

