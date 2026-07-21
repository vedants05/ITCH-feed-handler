#include <linux/bpf.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/udp.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>
#include <linux/in.h>

// This map tells the kernel which XDP socket to redirect packets to
struct {
    __uint(type, BPF_MAP_TYPE_XSKMAP);
    __uint(max_entries, 64);
    __type(key, int);
    __type(value, int);
} xsks_map SEC(".maps");

SEC("xdp")
int xdp_filter(struct xdp_md *ctx) {
    // ctx->data and ctx->data_end are pointers to the raw packet bytes
    void *data     = (void *)(long)ctx->data;
    void *data_end = (void *)(long)ctx->data_end;

    // parse Ethernet header
    struct ethhdr *eth = data;
    if ((void *)(eth + 1) > data_end) return XDP_PASS;  // too short, pass to kernel
    if (eth->h_proto != bpf_htons(ETH_P_IP)) return XDP_PASS;  // not IPv4, pass

    // parse IP header
    struct iphdr *ip = (void *)(eth + 1);
    if ((void *)(ip + 1) > data_end) return XDP_PASS;
    if (ip->protocol != IPPROTO_UDP) return XDP_PASS;  // not UDP, pass

    // parse UDP header
    struct udphdr *udp = (void *)(ip + 1);
    if ((void *)(udp + 1) > data_end) return XDP_PASS;

    // only redirect packets on port 21002
    if (udp->dest != bpf_htons(21002)) return XDP_PASS;

    // redirect to our XDP socket
    return bpf_redirect_map(&xsks_map, ctx->rx_queue_index, XDP_DROP);
}

char _license[] SEC("license") = "GPL";
