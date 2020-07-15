#include <boost/archive/iterators/base64_from_binary.hpp>
#include <boost/archive/iterators/binary_from_base64.hpp>
#include <boost/archive/iterators/transform_width.hpp>
#include "spdlog/spdlog.h"
#include "Poco/Net/HTMLForm.h"
#include "Poco/Net/HTTPRequest.h"
#include "Poco/Net/PartHandler.h"
#include "Poco/StreamCopier.h"
#include "http_server.h"
#include "transform_stream_api.h"
#include "timercpp.h"

namespace Poco
{
    namespace Net
    {
        class StringPartHandler : public PartHandler
        {
        public:
            StringPartHandler()
            {
            }

            void handlePart(const MessageHeader &header, std::istream &stream)
            {
                std::string disp = header["Content-Disposition"];
                disp = disp.substr(disp.find_first_of(';') + 1);
                std::vector<std::string> infoVec = HttpServer::StringSplit(disp, ";");
                std::map<std::string, std::string> disp_map;
                for (std::string &disp_str : infoVec)
                {
                    int pos = disp_str.find_first_of('=');
                    std::string key = disp_str.substr(0, pos);
                    key.erase(std::remove(key.begin(), key.end(), ' '), key.end());
                    key.erase(std::remove(key.begin(), key.end(), '"'), key.end());

                    std::string value = disp_str.substr(pos + 1);
                    value.erase(std::remove(value.begin(), value.end(), ' '), value.end());
                    value.erase(std::remove(value.begin(), value.end(), '"'), value.end());
                    disp_map[key] = value;
                }
                auto ilter = disp_map.find("name");
                if (ilter == disp_map.end())
                {
                    return;
                }
                std::string name = ilter->second;

                ilter = disp_map.find("filename");
                if (ilter == disp_map.end())
                {
                    return;
                }
                std::string filename = ilter->second;

                std::ostringstream ostr;
                Poco::StreamCopier::copyStream(stream, ostr);
                part_map_.insert(std::make_pair(name, std::make_pair(filename, ostr.str())));
            }

            bool HasFiled(const std::string &name)
            {
                return part_map_.find(name) != part_map_.end();
            }

            const std::pair<std::string, std::string> &FileData(const std::string &name) const
            {
                auto ilter = part_map_.find(name);
                if (ilter != part_map_.end())
                {
                    return ilter->second;
                }

                return std::pair<std::string, std::string>();
            }

            const std::map<std::string, std::pair<std::string, std::string>> &PartMap() const
            {
                return part_map_;
            }

        private:
            std::map<std::string, std::pair<std::string, std::string>> part_map_;
        };
    } // namespace Net
} // namespace Poco

HttpServer::HttpServer(const std::string &host, const int thr_num) : io_service_(thr_num), io_work_(io_service_)
{
    for (int i = 0; i < thr_num; i++)
    {
        threads_vec_.emplace_back(std::thread([this] {
            io_service_.run();
        }));
    }
    quit_.store(false);
    io_service_.post(std::bind(&HttpServer::ClearDeadStream, this));
    listener_ = http_listener(host);
    listener_.support(std::bind(&HttpServer::OnRequest, this, std::placeholders::_1));
    handler_map_.insert(std::make_pair("/rest/api/v1/transform_stream", std::bind(&HttpServer::HandStart, this, std::placeholders::_1)));
    handler_map_.insert(std::make_pair("/rest/api/v1/stop", std::bind(&HttpServer::HandStop, this, std::placeholders::_1)));
}

HttpServer::~HttpServer()
{
    quit_.store(true);
    io_service_.stop();
    while (!io_service_.stopped())
    {
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
}

std::string HttpServer::EndPoint()
{
    return listener_.uri().to_string();
}

pplx::task<void> HttpServer::Accept()
{
    return listener_.open();
}

pplx::task<void> HttpServer::Shutdown()
{
    return listener_.close();
}

void HttpServer::SetTransformApi(const std::shared_ptr<TransformStreamApi> &ptr)
{
    transform_api_ = ptr;
}

std::vector<std::string> HttpServer::StringSplit(const std::string &s, const std::string &delim)
{
    std::vector<std::string> ret;
    size_t last = 0;
    size_t index = s.find_first_of(delim, last);
    while (index != std::string::npos)
    { //查找到匹配
        ret.push_back(std::string{s.substr(last, index - last)});
        last = index + 1;
        index = s.find_first_of(delim, last);
    }
    if (index - last > 0)
        ret.push_back(std::string{s.substr(last, index - last)});

    return ret;
}

void HttpServer::OnRequest(http_request message)
{
    std::string url_path = message.relative_uri().path();
    spdlog::info("Received request, path: {}", url_path);

    if (message.method() == methods::POST || message.method() == methods::GET)
    {
        io_service_.post([=] {
            try
            {
                std::function<void(http_request)> handler;
                bool is_valid_path = false;
                {
                    std::lock_guard<std::mutex> lock(hander_mtx_);
                    auto iter = handler_map_.find(url_path);
                    if (iter != handler_map_.end())
                    {
                        handler = iter->second;
                        is_valid_path = true;
                    }
                }

                if (is_valid_path)
                {
                    handler(message);
                }
                else
                {
                    auto response = json::value::object();
                    response["status"] = json::value::number(20001);

                    std::string msg("request an unknown uri: ");
                    msg += url_path;

                    response["message"] = json::value::string(msg);
                    message.reply(status_codes::BadRequest, response);

                    spdlog::trace("unknown path: {}", url_path);
                }
            }
            catch (std::exception &e)
            {
                auto response = json::value::object();
                response["status"] = json::value::number(20001);
                response["message"] = json::value::string(e.what());
                message.reply(status_codes::InternalError, response);

                spdlog::error("{}:{}", __FUNCTION__, e.what());
            }
            catch (...)
            {
                auto response = json::value::object();
                response["status"] = json::value::number(20001);
                response["message"] = json::value::string("unknown error");
                message.reply(status_codes::InternalError, response);

                spdlog::error("{}: unknown error", __func__);
            }
        });
    }
    else
    {
        auto response = json::value::object();
        response["status"] = json::value::number(20001);
        response["message"] = json::value::string("unsupported methord");
        message.reply(status_codes::NotImplemented, response);

        spdlog::warn("NotImplemented methord called");
    }
}

void HttpServer::HandStart(http_request message)
{
    io_service_.post([=] {
        try
        {
            auto result = uri::split_query(message.relative_uri().query());
            auto iter = result.find("url");
            if (iter == result.end())
            {
                auto response = json::value::object();
                response["status"] = 404;
                response["message"] = json::value::string("url not find");
                message.reply(status_codes::NotFound, response);
            }
            iter = result.find("auto-replay");
            bool auto_replay = false;
            if(iter != result.end())
            {
                auto_replay = true;
            }

            std::string input_url = iter->second;
            std::string out_url, err;
            transform_api_->start(input_url, out_url, [&, message, input_url, auto_replay](int code, const std::string out_url, const std::string &err) -> void {
                if (code == -1)
                {
                    auto response = json::value::object();
                    response["status"] = 20001;
                    response["message"] = json::value::string(err);
                    message.reply(status_codes::OK, response);

                    std::lock_guard<std::mutex> lock(url_mtx_);
                    dead_video_param_ = std::make_pair(input_url, "");
                    cv_.notify_one();
                }
                else if (code == 0)
                {
                    auto response = json::value::object();
                    response["status"] = 200;
                    response["message"] = json::value::string(err);
                    response["data"] = json::value::string(out_url);
                    message.reply(status_codes::OK, response);
                }
                else if (code == -2)
                {
                    std::lock_guard<std::mutex> lock(url_mtx_);
                    dead_video_param_ = std::make_pair(input_url, auto_replay ? out_url : "");
                    cv_.notify_one();
                }
            });
        }
        catch (const std::exception &e)
        {
            spdlog::error("HttpServer::HandTest exception {}", e.what());
        }
    });
}

void HttpServer::HandStop(http_request message)
{
    io_service_.post([=] {
        try
        {
            auto result = uri::split_query(message.relative_uri().query());
            auto iter = result.find("url");
            if (iter == result.end())
            {
                auto response = json::value::object();
                response["status"] = 404;
                response["message"] = json::value::string("url not find");
                message.reply(status_codes::NotFound, response);
            }

            std::string input_url = iter->second;
            std::string erroStr;
            transform_api_->stop(input_url, erroStr);
            auto response = json::value::object();
            if (erroStr.empty())
            {
                response["status"] = 200;
                response["message"] = json::value::string("successful");
            }
            else
            {
                response["status"] = 20001;
                response["message"] = json::value::string(erroStr);
            }
            message.reply(status_codes::OK, response);
        }
        catch (const std::exception &e)
        {
            spdlog::error("HttpServer::HandTest exception {}", e.what());
        }
    });
}

void HttpServer::Base64Encode(const std::string &input, std::string &output)
{
    typedef boost::archive::iterators::base64_from_binary<boost::archive::iterators::transform_width<std::string::const_iterator, 6, 8>> Base64EncodeIterator;
    std::stringstream result;
    try
    {
        std::copy(Base64EncodeIterator(input.begin()), Base64EncodeIterator(input.end()), std::ostream_iterator<char>(result));
    }
    catch (...)
    {
        return;
    }
    size_t equal_count = (3 - input.length() % 3) % 3;
    for (size_t i = 0; i < equal_count; i++)
    {
        result.put('=');
    }
    output = result.str();
}

void HttpServer::Base64Decode(const std::string &input, std::string &output)
{
    typedef boost::archive::iterators::transform_width<boost::archive::iterators::binary_from_base64<std::string::const_iterator>, 8, 6> Base64DecodeIterator;
    std::stringstream result;
    try
    {
        std::copy(Base64DecodeIterator(input.begin()), Base64DecodeIterator(input.end()), std::ostream_iterator<char>(result));
    }
    catch (...)
    {
    }
    output = result.str();
}

void HttpServer::ClearDeadStream()
{
    while (!quit_.load())
    {
        std::unique_lock<std::mutex> lock(url_mtx_);
        cv_.wait(lock, [=] { return !dead_video_param_.first.empty(); });
        std::string error;
        transform_api_->stop(dead_video_param_.first, error);
        if (!dead_video_param_.second.empty())
        {
            std::pair<std::string, std::string> temp = dead_video_param_;
            Timer *t = new Timer;
            t->setTimeout([&, temp, t] {
                std::string out_url = temp.second;
                transform_api_->start(temp.first, out_url, [&, temp](int code, const std::string out_url, const std::string &err) {
                    if (code == -2)
                    {
                        std::lock_guard<std::mutex> lock(url_mtx_);
                        dead_video_param_ = temp;
                        cv_.notify_one();
                    }
                    else if (code == -1)
                    {
                        std::lock_guard<std::mutex> lock(url_mtx_);
                        dead_video_param_ = temp;
                        cv_.notify_one();
                    }
                });
                delete t;
            },5000);
        }
        dead_video_param_ = std::make_pair("", "");
    }
}
