# syntax=docker/dockerfile:1
# Optional: docker build --platform linux/amd64 -t uvk5 .
FROM --platform=$BUILDPLATFORM archlinux:latest
RUN pacman -Syyu base-devel --noconfirm
RUN pacman -Syyu arm-none-eabi-gcc --noconfirm
RUN pacman -Syyu arm-none-eabi-newlib --noconfirm
RUN pacman -Syyu git --noconfirm
RUN pacman -Syyu python-pip --noconfirm
RUN pacman -Syyu python-crcmod --noconfirm
WORKDIR /app
COPY . .

RUN git submodule update --init --recursive
#RUN make && cp firmware* compiled-firmware/
