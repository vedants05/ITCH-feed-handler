#include <liburing.h>
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

static const int PORT        = 21002;
static const int QUEUE_DEPTH = 64;
static const int BUF_SIZE    = 2048;

static inline uint64_t now_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

int main() {

    // 1. create and bind UDP socket 
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) { perror("socket"); return 1; }

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(PORT);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        perror("bind"); return 1;
    }

    // 2. initialise io_uring
    struct io_uring ring;
    if (io_uring_queue_init(QUEUE_DEPTH, &ring, 0) < 0) {
        perror("io_uring_queue_init"); return 1;
    }

    // 3. allocate buffer pool — one buffer per queue slot
    uint8_t bufs[QUEUE_DEPTH][BUF_SIZE];

    // 4. submit initial batch of recv requests — fill all 64 slots
    for (int i = 0; i < QUEUE_DEPTH; i++) {
        struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
        io_uring_prep_recv(sqe, sock, bufs[i], BUF_SIZE, 0);
        io_uring_sqe_set_data64(sqe, i);  // tag with buffer index
    }
    io_uring_submit(&ring);  // ONE syscall submits all 64

    printf("io_uring receiver listening on port %d\n", PORT);

    // 5. completion loop

    ItchParser parser;
    std::vector<uint64_t> latencies;
    latencies.reserve(20000);
    bool running = true;

    while (running) {

        // wait for a completion — blocks until a tray is filled
        // this is NOT a syscall — just reads from shared memory
        struct io_uring_cqe *cqe;
        io_uring_wait_cqe(&ring, &cqe);

        // which buffer did the packet land in
        int buf_idx = (int)io_uring_cqe_get_data64(cqe);

        // how many bytes arrived
        int n = cqe->res;

        if (n > 0) {
            uint64_t t0 = now_ns();
            parser.process(bufs[buf_idx], (size_t)n);
            uint64_t t1 = now_ns();
            latencies.push_back(t1 - t0);

            // check for market close
            if (bufs[buf_idx][0] == 'S' && n >= 12 && bufs[buf_idx][11] == 'C') {
                printf("Market close received. Stopping.\n");
                running = false;
            }
        }

        // mark this completion as consumed — tray is empty again
        io_uring_cqe_seen(&ring, cqe);

        // resubmit this buffer slot for the next packet
        struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
        io_uring_prep_recv(sqe, sock, bufs[buf_idx], BUF_SIZE, 0);
        io_uring_sqe_set_data64(sqe, buf_idx);
        io_uring_submit(&ring);
    }

    // 6. cleanup
    io_uring_queue_exit(&ring);
    close(sock);

    // 7. print results 
    printf("\n=== Latency Results (io_uring) ===\n");
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
        printf("p50:    %lu ns\n",  percentile(50));
        printf("p99:    %lu ns\n",  percentile(99));
        printf("p99.9:  %lu ns\n",  percentile(99.9));
        printf("max:    %lu ns\n",  latencies.back());

        OrderBook* book = parser.get_book(1);
        if (book) {
            BestBidOffer bbo = book->best_bid_offer();
            printf("\n=== AAPL Order Book ===\n");
            printf("Bid levels:  %zu\n", book->bid_levels());
            printf("Ask levels:  %zu\n", book->ask_levels());
            printf("Live orders: %zu\n", book->order_count());
            if (bbo.bid_price > 0)
                printf("Best bid: %.4f  qty %lu\n",
                       bbo.bid_price / 10000.0, bbo.bid_qty);
            if (bbo.ask_price > 0)
                printf("Best ask: %.4f  qty %lu\n",
                       bbo.ask_price / 10000.0, bbo.ask_qty);
        }
    }

    return 0;
}