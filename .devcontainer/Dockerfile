FROM amazonlinux:latest

RUN yum update -y && yum install -y git
RUN yum install -y zip unzip tar

# Setup Vcpkg
RUN git clone https://github.com/microsoft/vcpkg.git
RUN cd vcpkg && ./bootstrap-vcpkg.sh
ENV PATH="/vcpkg:$PATH"

# Install Cmake and Ninja
RUN yum install -y cmake ninja-build

# Install AWS Dependencies
RUN dnf install -y kernel-devel
RUN yum install -y libcurl-devel openssl-devel libuuid-devel pulseaudio-libs-devel

# Install Clang
RUN dnf install -y clang clang-tools-extra

# Install Valgrind
RUN dnf install -y valgrind

# Setup Python
RUN dnf install -y python3.9-pip
RUN pip install --ignore-installed pipenv
ENV PIPENV_VENV_IN_PROJECT=1

# Install Node.js 22, NPM & CDK
RUN dnf install -y nodejs22 nodejs22-npm
RUN npm install -g aws-cdk

# Install Go 
ARG GO_VERSION=1.26.0
RUN curl -fsSL https://go.dev/dl/go${GO_VERSION}.linux-amd64.tar.gz | tar -C /usr/local -xz
ENV PATH="/usr/local/go/bin:$PATH"
RUN go install golang.org/x/tools/gopls@latest
