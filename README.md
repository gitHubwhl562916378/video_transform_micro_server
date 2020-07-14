# Video_Transform_Micro_Server
＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝
#nbsp#nbsp一个实现视频源转换的微服务。 如果输出地址设置为路径，则转出的文件在服务端访路径下;如果输出地址设置为流媒体地址，则转换为网络流；  
最终输出文件都以http返回

# How To Build
## 安装环境
>+ spdlog [https://github.com/gabime/spdlog](https://github.com/gabime/spdlog)
>+ libavcodec 58.22.101以上#nbsplibavformat 58.17.101以上#nbsplibavutil 56.19.100以上
>+ poco [https://github.com/pocoproject/poco](https://github.com/pocoproject/poco) 51以上
## 编译
1. `mkdir build`
2. `cd build`
3. `cmake .. && make`

# Other
## version
+ v1.0
## Update
[2020/7/14] 主要功能完成，能完成流转换。通过配置文件读取，实现ffmpeg的转换方法