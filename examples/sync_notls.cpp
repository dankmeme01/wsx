
#include <wsx/Client.hpp>
#include <print>
#include <iostream>

int main(int argc, const char** argv) {
    std::string url = "ws://localhost:8080";
    if (argc > 1) {
        url = argv[1];
    }

    auto result = wsx::connect(url);
    if (!result) {
        std::println("Connection failed: {}", result.unwrapErr());
        return 1;
    }

    wsx::Client client = std::move(result).unwrap();

    while (true) {
        std::cout << "> " << std::flush;
        std::string line;
        if (!std::getline(std::cin, line)) {
            break;
        }

        auto sendResult = client.send(line);
        if (!sendResult) {
            std::println("Send failed: {}", sendResult.unwrapErr());
            break;
        }

        auto recvResult = client.recv();
        if (!recvResult) {
            std::println("Receive failed: {}", recvResult.unwrapErr());
            break;
        }

        wsx::Message msg = std::move(recvResult).unwrap();
        if (msg.isText()) {
            std::println("{}", msg.text());
        } else {
            std::println("Received binary message of {} bytes", msg.data().size());
        }
    }

    std::println("Closing..");
    auto res = client.close();
    if (!res) {
        std::println("Close failed: {}", res.unwrapErr());
        return 1;
    }
}