#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstdlib>
#include <cstring>
#include <cstdio>

#include "itch_messages.hpp"
#include "itch_parser.hpp"

int main() {

    // 1. Create UDP socket
    int sock = socket(AF_INET, SOCK_DGRAM, 0); // AF_INET = IPv4, //SOCK_DGRAM = UDP,

    if (sock < 0) {
        perror("socket failed");
        return 1;
    }

    // 2. set up destination
    sockaddr_in dest{};
    dest.sin_family      = AF_INET;
    dest.sin_port        = htons(21002); // Converts port no to big-endian
    dest.sin_addr.s_addr = inet_addr("127.0.0.1");

    // Lambda to send messages
    auto send_msg = [&](const void* msg, size_t len) {
        sendto(sock, msg, len, 0, reinterpret_cast<const sockaddr*>(&dest), sizeof(dest));
    };

    // 3. send market open
    SystemEventMessage open_msg{};
    open_msg.msg_type   = 'S';
    open_msg.event_code = 'O';
    send_msg(&open_msg, sizeof(open_msg));


    // 4. send stock directory
    StockDirectoryMessage dir_msg{};
    dir_msg.msg_type     = 'R';
    dir_msg.stock_locate = htons(1);        // locate code 1 = AAPL
    memcpy(dir_msg.stock, "AAPL    ", 8);  // space padded to 8 bytes
    dir_msg.round_lot_size = htonl(100);
    send_msg(&dir_msg, sizeof(dir_msg));

    // 5. send orders
    int num_orders = 10000;
    srand(42);  // fixed seed so results are reproducible

    for (int i = 0; i < num_orders; i++) {
        AddOrderMessage add_msg{};
        add_msg.msg_type     = 'A';
        add_msg.stock_locate = htons(1);

        // unique reference number for each order
        uint64_t ref = 1000 + i;

        // htobe64 is the mac version
        add_msg.order_ref_num = htobe64_portable(ref);

        // alternate buy and sell
        add_msg.buy_sell_indicator = (i % 2 == 0) ? 'B' : 'S';

        // random quantity between 100 and 1000
        uint32_t shares = (rand() % 10 + 1) * 100;
        add_msg.shares = htonl(shares);

        // mid price is £182.30 = 1823000
        // buys slightly below mid, sells slightly above
        uint32_t offset = rand() % 500;  // up to £0.05 offset
        uint32_t price;
        if (add_msg.buy_sell_indicator == 'B')
            price = 1823000 - offset;
        else
            price = 1823000 + offset;
        add_msg.price = htonl(price);

        memcpy(add_msg.stock, "AAPL    ", 8);

        send_msg(&add_msg, sizeof(add_msg));
    }

    for (int i = 0; i < num_orders; i += 10) {
        DeleteOrderMessage del_msg{};
        del_msg.msg_type     = 'D';
        del_msg.stock_locate = htons(1);
        del_msg.order_ref_num = htobe64_portable(1000 + i);
        send_msg(&del_msg, sizeof(del_msg));
    }

    // 6. send market close
    SystemEventMessage close_msg{};
    close_msg.msg_type   = 'S';
    close_msg.event_code = 'C';
    send_msg(&close_msg, sizeof(close_msg));

    // 7. close socket
    close(sock);
    printf("Sender done.\n");

    return 0;
}