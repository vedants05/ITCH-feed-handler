#include <bpf/libbpf.h>
#include <bpf/xsk.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <netinet/in.h>
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

