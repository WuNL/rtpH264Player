cmake_minimum_required(VERSION 3.13)
project(rtpH264)

set(CMAKE_CXX_STANDARD 11)


add_executable(rtpH264 ffHeaders.h main.cpp rtpH264.h rtpH264.cpp)

target_link_libraries(rtpH264 avdevice avcodec avfilter avformat swscale swresample avutil z m dl pthread)