#include "testing/testing.hpp"
#include "prime_server.hpp"
#include "http_protocol.hpp"

#include <unistd.h>
#include <functional>
#include <memory>
#include <unordered_set>
#include <thread>
#include <iterator>
#include <cstdlib>
#include <cstring>

using namespace prime_server;

namespace {
  class testable_http_server_t : public http_server_t {
   public:
    using http_server_t::http_server_t;
    using http_server_t::enqueue;
    using http_server_t::request_id;
    //zmq is great, it will hold on to unsent messages so that if you are disconnected
    //and reconnect, they eventually do get sent, for this test we actually want them
    //dropped since we arent really testing their delivery here
    void passify() {
      int disabled = 0;
      proxy.setsockopt(ZMQ_LINGER, &disabled, sizeof(disabled));
    }
  };

  struct testable_http_request_t : public http_request_t {
   public:
    using http_request_t::partial_buffer;
  };

  class testable_http_client_t : public http_client_t {
   public:
    using http_client_t::http_client_t;
    using http_client_t::stream_responses;
    using http_client_t::buffer;
  };

  void test_streaming_server() {
    zmq::context_t context;
    testable_http_server_t server(context, "ipc://test_http_server", "ipc://test_http_proxy_upstream", "ipc://test_http_results");
    server.passify();

    testable_http_request_t request;
    std::string incoming("GET /irgendwelle/pfad HTTP/1.1\r");
    server.enqueue(static_cast<const void*>(incoming.data()), incoming.size(), "irgendjemand", request);
    incoming ="\nContent-Length: 7\r\n\r\ngohtlosGET /annrer/pfad?aafrag=gel HTTP/1.0\r\n\r\nsell_siehscht_du_au_noed";
    server.enqueue(static_cast<const void*>(incoming.data()), incoming.size(), "irgendjemand", request);

    if(server.request_id != 2)
      throw std::runtime_error("Wrong number of requests were forwarded");
    if(request.partial_buffer != "sell_siehscht_du_au_noed")
      throw std::runtime_error("Unexpected partial request data");


  }

  void test_streaming_client() {
    std::string all;
    size_t responses = 0;
    zmq::context_t context;
    testable_http_client_t client(context, "ipc://test_http_server",
      [](){ return std::make_pair<void*, size_t>(nullptr, 0); },
      [&all, &responses](const void* data, size_t size){
        std::string response(static_cast<const char*>(data), size);
        all.append(response);
        ++responses;
        return true;
      });

    bool more;
    std::string incoming = "HTTP/1.0 OK\r\nContent-Lengt";
    auto reported_responses = client.stream_responses(static_cast<const void*>(incoming.data()), incoming.size(), more);
    incoming = "h: 6\r\n\r\nguet\r";
    reported_responses += client.stream_responses(static_cast<const void*>(incoming.data()), incoming.size(), more);

    if(all != "")
      throw std::runtime_error("Unexpected response data");

    incoming = "\nHTTP/1.0 OK\r\n\r\nsell siehscht du au noed";
    reported_responses += client.stream_responses(static_cast<const void*>(incoming.data()), incoming.size(), more);

    if(!more)
      throw std::runtime_error("Expected the client to want more responses");
    if(all != "HTTP/1.0 OK\r\nContent-Length: 6\r\n\r\nguet\r\nHTTP/1.0 OK\r\n\r\n")
      throw std::runtime_error("Unexpected response data");
    if(responses != 2)
      throw std::runtime_error("Wrong number of responses were collected");
    if(reported_responses != responses)
      throw std::runtime_error("Wrong number of responses were reported");
    if(client.buffer != "sell siehscht du au noed")
      throw std::runtime_error("Unexpected partial response data");
  }


  void test_request() {
    std::string http = http_request_t::to_string(method_t::GET, "e_chliises_schtoeckli");
    if(http != "GET e_chliises_schtoeckli HTTP/1.1\r\n\r\n")
      throw std::runtime_error("Request was not well-formed");
  }

  void test_request_parsing() {
    std::string request_str("GET /wos_haescht?nen_stei=2&ne_bluem=3&ziit=5%20minuet HTTP/1.0\r\nHost: localhost:8002\r\nUser-Agent: ApacheBench/2.3\r\n\r\n");
    auto request = http_request_t::from_string(request_str.c_str(), request_str.size());
    //TODO: tighten up this test
    if(request.method != method_t::GET)
      throw std::runtime_error("Request parsing failed");
    if(request.path != "/wos_haescht")
      throw std::runtime_error("Request parsing failed");
    if(request.version != "HTTP/1.0")
      throw std::runtime_error("Request parsing failed");
    /*
    if(request.query != query_t{ {"nen_stei", {"2"}}, {"ne_bluem", {"3"}}, {"ziit", {"5%20minuet"}} })
    if(request.headers != headers_t{ {"Host", "localhost:8002"}, {"User-Agent", "ApacheBench/2.3"} })
    */
    if(request.body != "")
      throw std::runtime_error("Request parsing failed");

    request_str = "GET /blah HTTP/1.0\r\nHost: *\r\nContent-Length: 5\r\nUser-Agent: fake-agent\r\n\r\nhello";
    request = http_request_t::from_string(request_str.c_str(), request_str.size());
    if(request.body != "hello")
      throw std::runtime_error("Request parsing failed");

    request_str = "POST /is_prime HTTP/1.1\r\nPragma: no-cache\r\nConnection: keep-alive\r\nContent-Type: text/xml; charset=UTF-8\r\nAccept-Encoding: gzip, deflate\r\nAccept-Language: de,en-US;q=0.7,en;q=0.3\r\nAccept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\nUser-Agent: Mozilla/5.0 (X11; Ubuntu; Linux x86_64; rv:35.0) Gecko/20100101 Firefox/35.0\r\nCache-Control: no-cache\r\nContent-Length: 11\r\nHost: localhost:8002\r\n\r\n32416190071";
    request = http_request_t::from_string(request_str.c_str(), request_str.size());
    if(request.body != "32416190071")
      throw std::runtime_error("Request parsing failed");
  }

  void test_query_parsing() {
    std::string path("/blah?");
    auto query = http_request_t::split_path_query(path);
    if(path != "/blah" || query.size() > 0)
      throw std::runtime_error("query parsing failed");

    path = "/blah?&&n&n=&n=b==c&=&a=1&1=2&x=y=z=4&=b&&";
    query = http_request_t::split_path_query(path);
    if(path != "/blah")
      throw std::runtime_error("wrong path");
    if(query.size() != 5)
      throw std::runtime_error("wrong keys");
    if(query[""] != query_t::value_type::second_type{"", "","","b", ""})
      throw std::runtime_error("wrong values");
    if(query["n"] != query_t::value_type::second_type{"", "", "b==c"})
      throw std::runtime_error("wrong values");
    if(query["a"] != query_t::value_type::second_type{"1"})
      throw std::runtime_error("wrong values");
    if(query["1"] != query_t::value_type::second_type{"2"})
      throw std::runtime_error("wrong values");
    if(query["x"] != query_t::value_type::second_type{"y=z=4"})
      throw std::runtime_error("wrong values");

    path = "/blah?foo=bar&foo=baz&case=simple";
    query = http_request_t::split_path_query(path);
    if(path != "/blah")
      throw std::runtime_error("wrong path");
    if(query.size() != 2)
      throw std::runtime_error("wrong keys");
    if(query["foo"] != query_t::value_type::second_type{"bar", "baz"})
      throw std::runtime_error("wrong values");
    if(query["case"] != query_t::value_type::second_type{"simple"})
      throw std::runtime_error("wrong values");
  }

  void test_response() {
    std::string http = http_response_t::generic(200, "OK", headers_t{}, "e_chliises_schtoeckli");
    if(http != "HTTP/1.1 200 OK\r\nContent-Length: 21\r\n\r\ne_chliises_schtoeckli")
      throw std::runtime_error("Response was not well-formed");
  }

  constexpr char alpha_numeric[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

  std::string random_string(size_t length) {
    std::string random(length, '\0');
    for(auto& c : random)
      c = alpha_numeric[rand() % (sizeof(alpha_numeric) - 1)];
    return random;
  }

  void http_client_work(zmq::context_t& context) {
    //client makes requests and gets back responses in a batch fashion
    const size_t total = 100000;
    std::unordered_set<std::string> requests;
    size_t received = 0;
    std::string request;
    http_client_t client(context, "ipc://test_http_server",
      [&requests, &request]() {
        //we want more requests
        if(requests.size() < total) {
          std::pair<std::unordered_set<std::string>::iterator, bool> inserted = std::make_pair(requests.end(), false);
          while(inserted.second == false) {
            request = random_string(10);
            inserted = requests.insert(request);
          }
          if(requests.size() % 2)
            request = http_request_t::to_string(method_t::GET, request);
          else
            request = http_request_t::to_string(method_t::POST, "", request);
        }//blank request means we are done
        else
          request.clear();
        return std::make_pair(static_cast<const void*>(request.c_str()), request.size());
      },
      [&requests, &received](const void* data, size_t size) {
        //get the result and tell if there is more or not
        std::string response(static_cast<const char*>(data), size);
        response = response.substr(response.rfind("\r\n\r\n") + 4);
        if(requests.find(response) == requests.end())
          throw std::runtime_error("Unexpected response!");
        return ++received < total;
      }, 100
    );
    //request and receive
    client.batch();
  }

  void test_parallel_clients() {

    zmq::context_t context;

    //server
    std::thread server(std::bind(&http_server_t::serve,
     http_server_t(context, "ipc://test_http_server", "ipc://test_http_proxy_upstream", "ipc://test_http_results")));
    server.detach();

    //load balancer for parsing
    std::thread proxy(std::bind(&proxy_t::forward,
      proxy_t(context, "ipc://test_http_proxy_upstream", "ipc://test_http_proxy_downstream")));
    proxy.detach();

    //echo worker
    std::thread worker(std::bind(&worker_t::work,
      worker_t(context, "ipc://test_http_proxy_downstream", "ipc://NONE", "ipc://test_http_results",
      [] (const std::list<zmq::message_t>& job, void* request_info) {
        //could be a get or a post
        auto request = http_request_t::from_string(static_cast<const char*>(job.front().data()), job.front().size());
        http_response_t response(200, "OK");
        if(request.method == method_t::POST)
          response.body = request.body;
        else if(request.method == method_t::GET)
          response.body = request.path;
        else
          throw std::runtime_error("Wrong method, get or post expected");
        response.from_info(*static_cast<http_request_t::info_t*>(request_info));

        //send it back
        worker_t::result_t result{false};
        result.messages.emplace_back(response.to_string());
        return result;
      }
    )));
    worker.detach();

    //make a bunch of clients
    std::thread client1(std::bind(&http_client_work, context));
    std::thread client2(std::bind(&http_client_work, context));
    client1.join();
    client2.join();
  }

}

int main() {
  //make this whole thing bail if it doesnt finish fast
  alarm(60);

  testing::suite suite("http");

  suite.test(TEST_CASE(test_streaming_client));

  suite.test(TEST_CASE(test_streaming_server));

  suite.test(TEST_CASE(test_request));

  suite.test(TEST_CASE(test_request_parsing));

  suite.test(TEST_CASE(test_query_parsing));

  suite.test(TEST_CASE(test_response));

  suite.test(TEST_CASE(test_parallel_clients));
  return suite.tear_down();
}
