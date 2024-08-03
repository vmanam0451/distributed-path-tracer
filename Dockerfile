FROM public.ecr.aws/amazonlinux/amazonlinux:latest

RUN yum update -y && yum install -y git && yum install -y git-lfs
RUN yum install -y zip unzip tar

# Setup Vcpkg

RUN git clone https://github.com/microsoft/vcpkg.git
RUN cd vcpkg && ./bootstrap-vcpkg.sh
ENV PATH="/vcpkg:$PATH"

# Install Cmake and Ninja
RUN yum install -y cmake ninja-build

# Install linux headers (needed for openssl <- used by aws lambda cpp runtime)
RUN dnf install -y kernel-devel

# Install LLVM
RUN dnf install -y clang

# Install python3.9
RUN yum install python39
RUN python3.9 -m ensurepip --upgrade
RUN pip3 install pipenv --user

WORKDIR /distributed-path-tracer
COPY . .