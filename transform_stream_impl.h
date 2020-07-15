#pragma once
#include <map>
#include <memory>
#include <atomic>
#include <thread>
#include <mutex>
#include "transform_stream_api.h"

class TransformStreamFFmpeg
{
public:
    TransformStreamFFmpeg() = default;
    ~TransformStreamFFmpeg();
    std::string src() const;
    std::string dstUrl() const;
    void start(const std::string input_url, const std::string output_url, const std::function<void(int, const std::string out_url, const std::string &err)> call_back);
    bool stop();

private:
    std::atomic_bool running_;
    std::string input_url_, output_url_;
    bool is_first_frame_ = true;
};

class TransformStream : public TransformStreamApi
{
public:
    TransformStream();
    void set_media_host(const std::string &host_addr) override;
    void start(const std::string &input_url, std::string &output_url, const std::function<void(int, const std::string out_url, const std::string &err)> call_back) override;
    void stop(const std::string &input_url, std::string &err) override;

private:
    std::mutex mtx_;
    std::atomic_int index_;
    std::string host_addr_;
    std::map<std::string, std::pair<std::shared_ptr<TransformStreamFFmpeg>, std::shared_ptr<std::thread>>> transforms_;
};