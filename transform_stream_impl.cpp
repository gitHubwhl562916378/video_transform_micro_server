#include <iostream>
#include <assert.h>
#include <spdlog/spdlog.h>

extern "C"
{
#include <libavutil/time.h>
#include <libavformat/avformat.h>
}
#include "transform_stream_impl.h"

TransformStreamFFmpeg::~TransformStreamFFmpeg()
{
}

std::string TransformStreamFFmpeg::src() const
{
	return input_url_;
}

std::string TransformStreamFFmpeg::dstUrl() const
{
	return output_url_;
}

extern std::string g_oformat;
void TransformStreamFFmpeg::start(const std::string rtsp_url, const std::string rtmp_url, const std::function<void(int, const std::string out_url, const std::string &err)> call_back)
{
	AVFormatContext *format_ctx;
	AVFormatContext *output_format = NULL;
	std::string erroStr;
	int ret;
	try
	{
		input_url_ = rtsp_url;
		output_url_ = rtmp_url;
		spdlog::info("input url: {}", rtsp_url);
		spdlog::info("output url: {}", rtmp_url);

		static bool is_inited = false;
		if (!is_inited)
		{
			av_register_all();
			avformat_network_init();
			av_log_set_level(AV_LOG_INFO);
			is_inited = true;
		}

		AVDictionary *opt = nullptr;
		//av_dict_set(&opt,"buffer_size","1024000",0);
		//av_dict_set(&opt,"max_delay","0",0);
		av_dict_set(&opt, "rtsp_transport", "tcp", 0);
		av_dict_set(&opt, "stimeout", "10000000", 0);

		spdlog::trace("create {} AVFormatContext", rtsp_url);
		format_ctx = avformat_alloc_context();

		spdlog::trace("open {}", rtsp_url);
		ret = avformat_open_input(&format_ctx, rtsp_url.c_str(), NULL, &opt);
		if (ret != 0)
		{
			erroStr = "open input failed error: ";
			erroStr += std::string(av_err2str(ret));
			spdlog::error("{} {}", rtsp_url, erroStr);
			avformat_free_context(format_ctx);
			call_back(-1, rtmp_url, erroStr);
			return;
		}

		ret = avformat_find_stream_info(format_ctx, NULL);
		spdlog::trace("wait... {}", rtsp_url);
		if (ret != 0)
		{
			erroStr = "open avformat_find_stream_info failed error: ";
			erroStr += av_err2str(ret);
			spdlog::error("{} {}", rtsp_url, erroStr);
			avformat_free_context(format_ctx);
			avformat_close_input(&format_ctx);
			call_back(-1, rtmp_url, erroStr);
			return;
		}

		av_dump_format(format_ctx, 0, rtsp_url.c_str(), 0);
		spdlog::trace("prepare output context {}", rtsp_url);

		ret = avformat_alloc_output_context2(&output_format, NULL, g_oformat.data(), rtmp_url.c_str());
		//avformat_alloc_output_context2(&output_format, NULL, "h264", rtmp_url.c_str());
		if (ret != 0)
		{
			erroStr = "open avformat_alloc_output_context2 failed error: ";
			erroStr += av_err2str(ret);
			spdlog::error("{} {}", rtsp_url, erroStr);
			avformat_free_context(format_ctx);
			avformat_free_context(output_format);
			avformat_close_input(&format_ctx);
			call_back(-1, rtmp_url, erroStr);
			return;
		}

		for (int i = 0; i < format_ctx->nb_streams; i++)
		{
			AVStream *out_stream = avformat_new_stream(output_format, NULL);
			assert(out_stream != NULL);

			AVStream *in_stream = format_ctx->streams[i];

			ret = avcodec_parameters_copy(out_stream->codecpar, format_ctx->streams[i]->codecpar);
			if (ret != 0)
			{
				erroStr = "avcodec_parameters_copy failed error: ";
				erroStr += av_err2str(ret);
				spdlog::error("{} {}", rtsp_url, erroStr);
				avformat_free_context(format_ctx);
				avformat_free_context(output_format);
				avformat_close_input(&format_ctx);
				call_back(-1, rtmp_url, erroStr);
				return;
			}

			out_stream->codecpar->codec_tag = 0;
		}

		av_dump_format(output_format, 0, rtmp_url.c_str(), 1);

		if (!(output_format->oformat->flags & AVFMT_NOFILE))
		{
			ret = avio_open(&output_format->pb, rtmp_url.c_str(), AVIO_FLAG_WRITE);
			if (ret != 0)
			{
				erroStr = "avio_open output failed error: ";
				erroStr += av_err2str(ret);
				spdlog::error("{} {}", rtsp_url, erroStr);
				avformat_free_context(format_ctx);
				avformat_free_context(output_format);
				avformat_close_input(&format_ctx);
				call_back(-1, rtmp_url, erroStr);
				return;
			}
		}

		ret = avformat_write_header(output_format, NULL);
		if (ret != 0)
		{
			erroStr = "avformat_write_header failed error: ";
			erroStr += av_err2str(ret);
			spdlog::error("{} {}", rtsp_url, erroStr);
			avformat_free_context(format_ctx);
			avformat_free_context(output_format);
			avformat_close_input(&format_ctx);
			avio_close(output_format->pb);
			call_back(-1, rtmp_url, erroStr);
			return;
		}

		int64_t start_time = av_gettime();
		AVPacket packet;
		running_.store(true);
		while (running_.load())
		{
			AVStream *in_stream, *out_stream;
			ret = av_read_frame(format_ctx, &packet);
			if (ret != 0)
			{
				break;
			}
			else if (ret == 0)
			{
				if (is_first_frame_)
				{
					call_back(0, rtmp_url, "successful");
					is_first_frame_ = false;
				}
			}

			in_stream = format_ctx->streams[packet.stream_index];
			out_stream = output_format->streams[packet.stream_index];

			if (in_stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
			{
				AVRational time_base = in_stream->time_base;
				AVRational time_base_q = AV_TIME_BASE_Q;
				int64_t pts_time = av_rescale_q(packet.dts, time_base, time_base_q);
				int64_t now_time = av_gettime() - start_time;
				if (pts_time > now_time)
					av_usleep(pts_time - now_time);
			}

			//Convert PTS/DTS
			//ac_rescale_q(a,b,c) = a * b / c
			packet.pts = av_rescale_q_rnd(packet.pts, in_stream->time_base, out_stream->time_base, (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
			packet.dts = av_rescale_q_rnd(packet.dts, in_stream->time_base, out_stream->time_base, (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
			packet.duration = av_rescale_q(packet.duration, in_stream->time_base, out_stream->time_base);

			av_write_frame(output_format, &packet);

			av_packet_unref(&packet);
		}
	}
	catch (const std::exception &e)
	{
		erroStr = e.what();
		spdlog::critical("TransformStreamFFmpeg {} exception {}", rtsp_url, erroStr);
	}
	av_write_trailer(output_format);

	if (!(output_format->oformat->flags & AVFMT_NOFILE))
	{
		avio_close(output_format->pb);
	}
	avformat_close_input(&format_ctx);

	avformat_free_context(output_format);
	avformat_free_context(format_ctx);

	avformat_network_deinit();
	if (running_.load())
	{
		if (ret != AVERROR_EOF)
		{
			if (is_first_frame_)
			{
				erroStr = "av_read_frame failed error: ";
				erroStr += av_err2str(ret);
				call_back(-1, rtmp_url, erroStr);
			}
			else
			{
				call_back(-2, rtmp_url, erroStr);
			}
		}
		else
		{
			call_back(-2, rtmp_url, erroStr);
		}
	}

	running_.store(false);
}

bool TransformStreamFFmpeg::stop()
{
	running_.store(false);
}

TransformStream::TransformStream()
{
	index_.store(0);
}

void TransformStream::set_media_host(const std::string &host_addr)
{
	host_addr_ = host_addr;
}

void TransformStream::start(const std::string &input_url, std::string &output_url, const std::function<void(int, const std::string out_url, const std::string &err)> call_back)
{
	std::lock_guard<std::mutex> lock(mtx_);
	auto iter = transforms_.find(input_url);
	if (iter != transforms_.end())
	{
		std::string err("current transform existsing");
		spdlog::warn("TransformStream::start {} {}", err, iter->second.first->dstUrl());
		call_back(0, iter->second.first->dstUrl(), err);
		return;
	}

	std::shared_ptr<TransformStreamFFmpeg> new_obj = std::make_shared<TransformStreamFFmpeg>();
	if (output_url.empty())
	{
		output_url = host_addr_ + "/" + std::to_string(index_++);
	}
	std::shared_ptr<std::thread> new_thr = std::make_shared<std::thread>(std::bind(&TransformStreamFFmpeg::start, new_obj.get(), input_url, output_url, call_back));
	bool code = transforms_.insert(std::make_pair(input_url, std::make_pair(new_obj, new_thr))).second;
	if (!code)
	{
		spdlog::error("TransformStream::start {} faild", input_url);
	}
}

void TransformStream::stop(const std::string &input_url, std::string &err)
{
	std::lock_guard<std::mutex> lock(mtx_);
	auto iter = transforms_.find(input_url);
	if (iter == transforms_.end())
	{
		err = "transform not exists";
		spdlog::warn("TransformStream::stop source {} transform not exists", input_url);
		return;
	}

	auto obj = iter->second.first;
	auto thr = iter->second.second;
	obj->stop();
	if (thr->joinable())
	{
		thr->join();
	}
	transforms_.erase(iter);
}