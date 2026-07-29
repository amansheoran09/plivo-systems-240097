/* SENDER — data + XOR parity FEC, byte-budget governed.
 *
 * 47010 <- harness source (4B BE seq + 160B payload)
 * 47001 -> relay (our wire format: [type:1][seq:2 BE][payload:160] = 163B)
 *
 * type 0 = data for seq
 * type 1 = parity for seq, payload = frame[seq] XOR frame[seq-STRIDE]
 */
#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/socket.h>
#include <unistd.h>

#define PAYLOAD 160
#define HDR 3
#define PKT (HDR + PAYLOAD)
#define MAXFRAMES 65536

static uint8_t hist[MAXFRAMES][PAYLOAD];
static uint8_t have[MAXFRAMES];

int main(void) {
    int stride = 1;
    double ratio = 1.95;
    const char *e;
    if ((e = getenv("FEC_STRIDE"))) stride = atoi(e);
    if ((e = getenv("FEC_RATIO")))  ratio  = atof(e);
    if (stride < 1) stride = 1;

    int in_fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in in_addr{};
    in_addr.sin_family = AF_INET;
    in_addr.sin_port = htons(47010);
    in_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    if (bind(in_fd, (struct sockaddr *)&in_addr, sizeof in_addr) < 0) {
        perror("bind 47010"); return 1;
    }

    int out_fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in relay{};
    relay.sin_family = AF_INET;
    relay.sin_port = htons(47001);
    relay.sin_addr.s_addr = inet_addr("127.0.0.1");

    uint8_t buf[2048], pkt[PKT];
    double bytes_sent = 0.0;

    for (;;) {
        ssize_t n = recvfrom(in_fd, buf, sizeof buf, 0, NULL, NULL);
        if (n != 4 + PAYLOAD) continue;
        uint32_t seq32;
        memcpy(&seq32, buf, 4);
        seq32 = ntohl(seq32);
        if (seq32 >= MAXFRAMES) continue;
        memcpy(hist[seq32], buf + 4, PAYLOAD);
        have[seq32] = 1;

        /* 1. data packet, immediately */
        pkt[0] = 0;
        uint16_t s16 = htons((uint16_t)seq32);
        memcpy(pkt + 1, &s16, 2);
        memcpy(pkt + 3, buf + 4, PAYLOAD);
        sendto(out_fd, pkt, PKT, 0, (struct sockaddr *)&relay, sizeof relay);
        bytes_sent += PKT;

        /* 2. parity packet, if the byte budget allows */
        if (seq32 >= (uint32_t)stride && have[seq32 - stride]) {
            double allowed = ratio * PAYLOAD * (double)(seq32 + 1);
            if (bytes_sent + PKT <= allowed) {
                pkt[0] = 1;
                memcpy(pkt + 1, &s16, 2);
                const uint8_t *a = hist[seq32];
                const uint8_t *b = hist[seq32 - stride];
                for (int i = 0; i < PAYLOAD; i++) pkt[3 + i] = a[i] ^ b[i];
                sendto(out_fd, pkt, PKT, 0, (struct sockaddr *)&relay,
                       sizeof relay);
                bytes_sent += PKT;
            }
        }
    }
    return 0;
}
