# Video_Transform_Micro_Server
＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝

一个实现视频源转换的微服务。 如果输出地址设置为路径，则转出的文件在服务端访路径下;如果输出地址设置为流媒体地址，则转换为网络流；  
最终输出文件都以http返回

# How To Build
## 环境
>+ spdlog [https://github.com/gabime/spdlog](https://github.com/gabime/spdlog)
>+ libavcodec 58.22.101以上; libavformat 58.17.101以上; libavutil 56.19.100以上
>+ poco [https://github.com/pocoproject/poco](https://github.com/pocoproject/poco) 51以上
>+ cpprestsdk [https://github.com/microsoft/cpprestsdk](https://github.com/microsoft/cpprestsdk) 2.10以上
>+ pkg-config `sudo apt-get install pkg-config`

## 编译
1. `mkdir build`
2. `cd build`
3. `cmake .. && make`

# Run
> `./video_transform_micro_server` 确定配置文件config.xml在同一目录
> transform_video

  方法 | 地址 | URL参数 | 返回
  ---- | ---- | ---- | ----
  GET  | /rest/api/v1/transform_stream | url=rtsp://192.168.2.66/video.avi | {code: 200, message: "successful", data: "rtmp://10.10.1.88/live/1"}  
  GET  | /rest/api/v1/stop | url=rtsp://192.168.2.66/video.avi&auto-replay=true | {code: 200, message: "successful"}  

# Other
## version
+ v1.0

## Update

### [2020/7/14]
1. 主要功能完成，使用ffmpeg完成流转换。
2. 集成到http服务
3. 编写配置文件

### [2020/7/14]
1. 添加自动重连参数，默认不启用，当启用时；视频播放结束后自动开始重播放，输出地址不变。第一次播放失败的，在启用情况下不会重播放；第一次播放成功的，后面不管是播放结束还是其它错误都会重播放