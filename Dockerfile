# Docker image that will allow to build brocky on an raspberry pi.
FROM raspbian/jessie

# Get common dependencies
RUN apt-get update
RUN apt-get install build-essential -y

# Get dependencies for boringssl
RUN apt-get install --fix-missing
RUN apt-get install libcurl3 -y
RUN apt-get install ninja-build cmake perl golang -y --fix-missing
RUN apt-get install git curl -y

# Get rust through rustup for quiche
ENV RUSTUP_UNPACK_RAM=200000000
RUN curl https://sh.rustup.rs -sSf | sh -s -- --profile minimal --default-toolchain nightly -y

# Prepare built folder
RUN mkdir /brocky
RUN mkdir /brocky/int
RUN mkdir /brocky/deps
WORKDIR /brocky

# Prepare quiche
RUN git clone https://github.com/cloudflare/quiche.git deps/quiche
WORKDIR /brocky/deps/quiche
RUN git checkout fdf25964e9a771a9ea73cc07e64e3a4abb162622
RUN git submodule init && git submodule update

# Build quiche
ENV PATH="$HOME/.cargo/bin:${PATH}"
RUN ls -al $HOME/.cargo/bin
RUN $HOME/.cargo/bin/cargo build

# Copy over source code.
COPY ./src /brocky/src
COPY ./CMakeLists.txt /brocky/CMakeLists.txt

# Build with cmake and ninja
WORKDIR /brocky
RUN cmake -G Ninja .
RUN ninja
RUN ls -al