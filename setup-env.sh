#!/bin/bash

# Update package lists
sudo apt-get update

# Install Boost and FFmpeg dependencies
sudo apt-get install -y libboost-all-dev ffmpeg libavcodec-dev libavformat-dev libavutil-dev libswscale-dev
