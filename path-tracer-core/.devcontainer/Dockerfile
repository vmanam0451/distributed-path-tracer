FROM amazonlinux:latest


RUN yum update -y && yum install -y git git-lfs
RUN yum install -y zip unzip tar

# Install Cmake and Ninja
RUN yum install -y cmake ninja-build

# Install LLVM
RUN dnf install -y clang clang-tools-extra

# Setup Vcpkg
RUN git clone https://github.com/microsoft/vcpkg.git
ENV VCPKG_FORCE_SYSTEM_BINARIES=1
RUN cd vcpkg && ./bootstrap-vcpkg.sh
ENV PATH="/vcpkg:$PATH"

# Install AWS Dependencies
RUN dnf install -y kernel-devel
RUN yum install -y libcurl-devel openssl-devel libuuid-devel pulseaudio-libs-devel

