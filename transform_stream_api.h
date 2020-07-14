#pragma once
#include <string>
#include <functional>

class TransformStreamApi
{
public:
    virtual ~TransformStreamApi(){};
    virtual void set_media_host(const std::string &host_addr) = 0;
    virtual void start(const std::string &input_url, std::string &output_url, const std::function<void(int, const std::string &err)> call_back) = 0;
    virtual void stop(const std::string &input_url, std::string &err) = 0;
};