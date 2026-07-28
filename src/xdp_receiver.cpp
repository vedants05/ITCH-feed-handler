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

// setup UMEM
static int setup_umem(XDPSocket& xdp) {
    // allocate 4MB of page-aligned memory for packet frames
    xdp.umem_area = mmap(nullptr, UMEM_SIZE,
                         PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB,
                         -1, 0);

    if (xdp.umem_area == MAP_FAILED) {
        // fallback to normal pages if hugepages unavailable
        xdp.umem_area = mmap(nullptr, UMEM_SIZE,
                             PROT_READ | PROT_WRITE,
                             MAP_PRIVATE | MAP_ANONYMOUS,
                             -1, 0);
        if (xdp.umem_area == MAP_FAILED) {
            perror("mmap"); return -1;
        }
        printf("Note: using normal pages (hugepages unavailable)\n");
    } else {
        printf("Using hugepages for UMEM\n");
    }

    // register the memory region with the kernel as a UMEM
    struct xsk_umem_config umem_cfg{};
    umem_cfg.fill_size      = RING_SIZE;
    umem_cfg.comp_size      = RING_SIZE;
    umem_cfg.frame_size     = FRAME_SIZE;
    umem_cfg.frame_headroom = 0;

    int ret = xsk_umem__create(&xdp.umem, xdp.umem_area, UMEM_SIZE,
                               &xdp.fill, nullptr, &umem_cfg);
    if (ret) {
        fprintf(stderr, "xsk_umem__create failed: %d\n", ret);
        return -1;
    }

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
    if (bpf_map_update_elem(map_fd, &key, &xsk_fd, BPF_ANY) < 0) {
        fprintf(stderr, "bpf_map_update_elem failed\n");
        return -1;
    }

    printf("XDP program loaded and attached to %s\n", INTERFACE);
    return 0;
}

