#pragma once
#include <cpprest/http_listener.h>
#include <boost/asio/io_service.hpp>
#include <thread>
using namespace web;
using namespace http;
using namespace http::experimental::listener;

class TransformStreamApi;
class HttpServer
{
public:
    HttpServer(const std::string &host, const int thr_num);
    ~HttpServer();
    std::string EndPoint();
    pplx::task<void> Accept();
    pplx::task<void> Shutdown();
    void SetTransformApi(const std::shared_ptr<TransformStreamApi>& ptr);
    static std::vector<std::string> StringSplit(const std::string &s, const std::string &delim);

private:
    void OnRequest(http_request);
    void HandStart(http_request);
    void HandStop(http_request);
    void Base64Encode(const std::string & input, std::string &output);
    void Base64Decode(const std::string &input, std::string &output);
    void ClearDeadStream();

    http_listener listener_;
    std::mutex hander_mtx_;
    std::map<std::string, std::function<void(http_request)>> handler_map_;

    boost::asio::io_service io_service_;
    boost::asio::io_service::work io_work_;
    std::vector<std::thread> threads_vec_;
    std::shared_ptr<TransformStreamApi> transform_api_;
    std::string dead_url_;
    std::mutex url_mtx_;
    std::condition_variable cv_;
    std::atomic_bool quit_;
};
