FROM ubuntu:lunar

RUN apt update && apt install -yq git git-lfs sudo cmake gcc-13 g++-13 && apt clean && rm -rf /var/lib/apt/lists/* /tmp/*

RUN useradd -l -u 33333 -G sudo -md /home/gitpod -s /bin/bash -p gitpod gitpod

USER gitpod
