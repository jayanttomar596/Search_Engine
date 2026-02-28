FROM ubuntu:22.04

RUN apt update && apt install -y \
    g++ \
    make \
    cmake \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY . .

RUN g++ -std=c++17 backend/server.cpp backend/SearchEngine.cpp backend/Trie.cpp -o server -pthread

EXPOSE 8080

CMD ["./server"]