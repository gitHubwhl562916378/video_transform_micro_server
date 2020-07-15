#include <iostream>
#include "spdlog/spdlog.h"
#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/sinks/daily_file_sink.h"
#include "Poco/Util/XMLConfiguration.h"
#include "http_server.h"
#include "factory.h"
#include "transform_stream_impl.h"
#define VERSION "V1.0"

std::mutex mtx;
std::condition_variable cv_;
void handleUserInterrupt(int signal){
    if (signal == SIGINT) {
        std::lock_guard<std::mutex> lock(mtx);
        std::cout << "SIGINT trapped ..." << std::endl;
        cv_.notify_one();
    }
}
std::string g_config_file = "config.xml";
std::string g_oformat;

int main()
{
    try{std::cout << SPDLOG_VERSION << std::endl;
        Poco::AutoPtr<Poco::Util::XMLConfiguration> configuration = new Poco::Util::XMLConfiguration;
        configuration->load(g_config_file);

        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();

        auto file_sink = std::make_shared<spdlog::sinks::daily_file_sink_mt>("logs/daily.txt", configuration->getInt("log.file[@update_h]"), configuration->getInt("log.file[@update_m]"));
        file_sink->set_level(spdlog::level::level_enum(SPDLOG_LEVEL_TRACE));

        spdlog::set_default_logger(std::shared_ptr<spdlog::logger>(new spdlog::logger("multi_sink", {console_sink, file_sink})));
        spdlog::set_level(spdlog::level::level_enum(SPDLOG_LEVEL_TRACE));

        Factory<TransformStreamApi, std::string> factory;
        factory.Register("ffmpeg", []() -> TransformStreamApi * { return new TransformStream; });
        
        TransformStreamApi *handle = factory.CreateObject(configuration->getString("video_transform[@transoform_use]"));
        handle->set_media_host(configuration->getString("video_transform[@media_server]"));
        g_oformat = configuration->getString("video_transform[@oformat]");

        HttpServer server("http://0.0.0.0:" + configuration->getString("http_server[@port]"), configuration->getInt("http_server[@threads]"));
        server.SetTransformApi(std::shared_ptr<TransformStreamApi>(handle));
        server.Accept().wait();
        spdlog::info("Video Transform Micro Server {} start listen on {}", VERSION, server.EndPoint());

        signal(SIGINT, handleUserInterrupt);
        std::unique_lock<std::mutex> lock(mtx);
        cv_.wait(lock);

        server.Shutdown().wait();
    }catch(std::exception &e){
        spdlog::critical("StartServer exception: {}", e.what());
    }
    return 0;
}
