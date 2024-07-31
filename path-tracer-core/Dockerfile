FROM public.ecr.aws/amazonlinux/amazonlinux:latest

RUN yum update -y && yum install -y git
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

WORKDIR /app
COPY . .

RUN cmake --preset x64-release-linux
RUN cmake --build --preset x64-release-linux

EXPOSE 6666
CMD ["./PathTracer"]