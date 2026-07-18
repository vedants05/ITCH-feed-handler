#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <vector>
#include <algorithm>
#include <numeric>

#include "itch_parser.hpp"

static inline uint64_t now_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

int main() {

    // 1. create UDP socket
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("socket failed");
        return 1;
    }

    // 2. bind to port 21002
    // binding tells the OS: give me all packets arriving on this port
    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(21002);
    addr.sin_addr.s_addr = INADDR_ANY;  // accept from any source IP

    if (bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        perror("bind failed");
        return 1;
    }

    printf("Receiver listening on port 21002...\n");
    printf("Waiting for market open...\n");

    // 3. parser and latency storage
    ItchParser parser;
    std::vector<uint64_t> latencies;
    latencies.reserve(20000);  // pre-allocate so vector doesn't resize mid-run

    // 4. receive loop
    uint8_t buf[2048];
    bool running = true;

    while (running) {

        // recvfrom() blocks here until a UDP packet arrives
        // the kernel copies the packet bytes into buf
        // n = how many bytes arrived
        ssize_t n = recvfrom(sock, buf, sizeof(buf), 0, nullptr, nullptr);
        if (n <= 0) continue;

        // timestamp before parsing
        uint64_t t0 = now_ns();

        // pass raw bytes to parser — updates the order book
        parser.process(buf, static_cast<size_t>(n));

        // timestamp after parsing
        uint64_t t1 = now_ns();

        latencies.push_back(t1 - t0);

        // check for market close
        // SystemEvent messages have msg_type 'S' at byte 0
        // event_code is at byte 11 — 'C' means close
        if (buf[0] == 'S' && n >= 12 && buf[11] == 'C') {
            printf("Market close received. Stopping.\n");
            running = false;
        }
    }

    close(sock);

    // 5. print latency results
    printf("\n=== Latency Results (recvfrom baseline) ===\n");
    printf("Total messages: %zu\n", latencies.size());

    if (!latencies.empty()) {
        std::sort(latencies.begin(), latencies.end());

        uint64_t sum = std::accumulate(latencies.begin(), latencies.end(), 0ULL);
        double mean  = static_cast<double>(sum) / latencies.size();

        auto percentile = [&](double p) -> uint64_t {
            size_t idx = static_cast<size_t>((p / 100.0) * latencies.size());
            if (idx >= latencies.size()) idx = latencies.size() - 1;
            return latencies[idx];
        };

        printf("mean:   %.1f ns\n", mean);
        printf("p50:    %llu ns\n", percentile(50));
        printf("p99:    %llu ns\n", percentile(99));
        printf("p99.9:  %llu ns\n", percentile(99.9));
        printf("max:    %llu ns\n", latencies.back());
    }

    // 6. print order book state for AAPL (locate code 1)
    OrderBook* book = parser.get_book(1);
    if (book) {
        BestBidOffer bbo = book->best_bid_offer();
        printf("\n=== AAPL Order Book ===\n");
        printf("Bid levels:  %zu\n", book->bid_levels());
        printf("Ask levels:  %zu\n", book->ask_levels());
        printf("Live orders: %zu\n", book->order_count());
        if (bbo.bid_price > 0)
            printf("Best bid: £%.4f  qty %llu\n",
                   bbo.bid_price / 10000.0, bbo.bid_qty);
        if (bbo.ask_price > 0)
            printf("Best ask: £%.4f  qty %llu\n",
                   bbo.ask_price / 10000.0, bbo.ask_qty);
    }

    return 0;
}