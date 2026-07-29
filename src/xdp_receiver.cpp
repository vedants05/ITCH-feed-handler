#include <bpf/libbpf.h>
#include <xdp/xsk.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <netinet/in.h>
#include <cstdlib>
#include <unistd.h>
#include <linux/if_link.h>
#include <net/if.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <vector>
#include <algorithm>
#include <numeric>
#include <string>

#include "itch_parser.hpp"

// constants 
static const char*   INTERFACE   = "enp0s1";
static const int     QUEUE_ID    = 0;
static const size_t  FRAME_SIZE  = 2048;
static const size_t  NUM_FRAMES  = 2048;
static const size_t  UMEM_SIZE   = FRAME_SIZE * NUM_FRAMES;  // 4MB
static const size_t  RING_SIZE   = 2048;

static inline uint64_t now_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

// XDP socket state 
struct XDPSocket {
    struct xsk_socket*          xsk;
    struct xsk_ring_cons        rx;
    struct xsk_ring_prod        fill;
    struct xsk_umem*            umem;
    void*                       umem_area;
};

static int setup_umem(XDPSocket& xdp) {

    // allocate 4MB of aligned memory
    if (posix_memalign(&xdp.umem_area, getpagesize(), UMEM_SIZE) != 0) {
        perror("posix_memalign");
        return -1;
    }

    memset(xdp.umem_area, 0, UMEM_SIZE);
    printf("UMEM allocated: %zu bytes\n", UMEM_SIZE);

    struct xsk_umem_config umem_cfg{};
    umem_cfg.fill_size      = RING_SIZE;
    umem_cfg.comp_size      = RING_SIZE;
    umem_cfg.frame_size     = FRAME_SIZE;
    umem_cfg.frame_headroom = 0;

    int ret = xsk_umem__create(&xdp.umem, xdp.umem_area, UMEM_SIZE,
                               &xdp.fill, nullptr, &umem_cfg);
				
	if (ret) {
		fprintf(stderr, "xsk_umem__create failed: %d (%s)\n", ret, strerror(-ret));
		return -1;
	}

    printf("UMEM registered with kernel\n");
    return 0;
}

// populate fill ring 
static void populate_fill_ring(XDPSocket& xdp, size_t count) {
    uint32_t idx;
    // reserve 'count' slots in the fill ring
    int ret = xsk_ring_prod__reserve(&xdp.fill, count, &idx);
    if (ret != (int)count) {
        fprintf(stderr, "fill ring reserve failed\n");
        return;
    }

    // write frame addresses into fill ring
    // kernel will use these frames to store incoming packets
    for (size_t i = 0; i < count; i++) {
        *xsk_ring_prod__fill_addr(&xdp.fill, idx++) = i * FRAME_SIZE;
    }

    xsk_ring_prod__submit(&xdp.fill, count);
}

// setup XDP socket 
static int setup_xsk(XDPSocket& xdp) {
    struct xsk_socket_config xsk_cfg{};
    xsk_cfg.rx_size      = RING_SIZE;
    xsk_cfg.tx_size      = RING_SIZE;
    xsk_cfg.libbpf_flags = 0;
    xsk_cfg.xdp_flags    = XDP_FLAGS_DRV_MODE;  // try driver mode first
    xsk_cfg.bind_flags   = XDP_USE_NEED_WAKEUP;

    int ret = xsk_socket__create(&xdp.xsk, INTERFACE, QUEUE_ID,
                                 xdp.umem, &xdp.rx, nullptr, &xsk_cfg);

    if (ret) {
        // fallback to SKB mode (works on any interface including virtual)
        printf("Driver mode failed, falling back to SKB mode\n");
        xsk_cfg.xdp_flags = XDP_FLAGS_SKB_MODE;
        ret = xsk_socket__create(&xdp.xsk, INTERFACE, QUEUE_ID,
                                 xdp.umem, &xdp.rx, nullptr, &xsk_cfg);
        if (ret) {
            fprintf(stderr, "xsk_socket__create failed: %d\n", ret);
            return -1;
        }
    }

    return 0;
}

// load and attach eBPF program 
static int load_xdp_program(XDPSocket& xdp) {
    // find xdp_program.o next to the executable
    std::string obj_path = "xdp_program.o";

    struct bpf_object* obj = bpf_object__open(obj_path.c_str());
    if (!obj) {
        fprintf(stderr, "bpf_object__open failed\n");
        return -1;
    }

    if (bpf_object__load(obj)) {
        fprintf(stderr, "bpf_object__load failed\n");
        return -1;
    }

    // find the xdp program inside the object
    struct bpf_program* prog = bpf_object__find_program_by_name(obj, "xdp_filter");
    if (!prog) {
        fprintf(stderr, "couldn't find xdp_filter program\n");
        return -1;
    }

    // attach to network interface
    int ifindex = if_nametoindex(INTERFACE);
    int prog_fd = bpf_program__fd(prog);
    if (bpf_xdp_attach(ifindex, prog_fd, XDP_FLAGS_SKB_MODE, nullptr) < 0) {
        fprintf(stderr, "bpf_xdp_attach failed\n");
        return -1;
    }

    // register our XDP socket in the eBPF map
    // the eBPF filter uses this map to know where to redirect packets
    struct bpf_map* map = bpf_object__find_map_by_name(obj, "xsks_map");
    if (!map) {
        fprintf(stderr, "couldn't find xsks_map\n");
        return -1;
    }

    int map_fd  = bpf_map__fd(map);
    int xsk_fd  = xsk_socket__fd(xdp.xsk);
    int key     = QUEUE_ID;
    
    if (bpf_map__update_elem(map, &key, sizeof(key), &xsk_fd, sizeof(xsk_fd), BPF_ANY) < 0) {
        fprintf(stderr, "bpf_map_update_elem failed\n");
        return -1;
    }

    printf("XDP program loaded and attached to %s\n", INTERFACE);
    return 0;
}

int main() {
    XDPSocket xdp{};

    // 1. set up UMEM — the shared packet buffer pool
    if (setup_umem(xdp) < 0) return 1;

    // 2. populate fill ring — give kernel all frames upfront
    populate_fill_ring(xdp, NUM_FRAMES / 2);

    // 3. create AF_XDP socket
    if (setup_xsk(xdp) < 0) return 1;

    // 4. load eBPF filter and register socket in map
    if (load_xdp_program(xdp) < 0) return 1;

    printf("XDP receiver running on %s\n", INTERFACE);
    printf("Waiting for market open...\n");

    // 5. receive loop
    ItchParser parser;
    std::vector<uint64_t> latencies;
    latencies.reserve(20000);
    bool running = true;

    while (running) {
        uint32_t idx_rx;
        // check how many packets are ready in the RX ring
        // this is just a memory read — no syscall
        unsigned int received = xsk_ring_cons__peek(&xdp.rx, 16, &idx_rx);

        if (received == 0) {
            // no packets yet — if socket needs wakeup, call recvfrom once
            // this is only needed in SKB mode
            if (xsk_ring_prod__needs_wakeup(&xdp.fill)) {
                recvfrom(xsk_socket__fd(xdp.xsk), nullptr, 0, MSG_DONTWAIT,
                         nullptr, nullptr);
            }
            continue;
        }

        for (unsigned int i = 0; i < received; i++) {
            // get the RX descriptor — tells us which frame and how many bytes
            const struct xdp_desc* desc = xsk_ring_cons__rx_desc(&xdp.rx, idx_rx++);

            // get pointer to packet data in UMEM
            // addr is the offset into our UMEM buffer
            uint8_t* pkt = (uint8_t*)xdp.umem_area + desc->addr;
            uint32_t len = desc->len;

            // skip Ethernet (14) + IP (20) + UDP (8) headers = 42 bytes
            // point directly at ITCH payload
            if (len > 42) {
                uint8_t* itch_payload = pkt + 42;
                uint32_t itch_len     = len - 42;

                uint64_t t0 = now_ns();
                parser.process(itch_payload, itch_len);
                uint64_t t1 = now_ns();
                latencies.push_back(t1 - t0);

                // check for market close
                if (itch_payload[0] == 'S' && itch_len >= 12 && itch_payload[11] == 'C') {
                    printf("Market close received. Stopping.\n");
                    running = false;
                }
            }

            // put this frame back in the fill ring for reuse
            uint32_t fill_idx;
            if (xsk_ring_prod__reserve(&xdp.fill, 1, &fill_idx) == 1) {
                *xsk_ring_prod__fill_addr(&xdp.fill, fill_idx) = desc->addr;
                xsk_ring_prod__submit(&xdp.fill, 1);
            }
        }

        // mark all consumed RX entries as done
        xsk_ring_cons__release(&xdp.rx, received);
    }

    // 6. print results
    printf("\n=== Latency Results (AF_XDP) ===\n");
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

    xsk_socket__delete(xdp.xsk);
    xsk_umem__delete(xdp.umem);
    munmap(xdp.umem_area, UMEM_SIZE);

    return 0;
}
