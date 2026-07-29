/* RECEIVER — dedupe, XOR-parity recovery, forward on first knowledge.
 *
 * 47002 <- media from sender via relay ([type:1][seq:2][payload:160])
 * 47020 -> harness player (4B BE seq + 160B payload)
 *
 * The player scores a frame on FIRST ARRIVAL BEFORE ITS DEADLINE, and never
 * penalises early arrival. So the optimal policy is: the instant a payload
 * becomes known, emit it. No playout clock is needed on this side.
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

static uint8_t data[MAXFRAMES][PAYLOAD];
static uint8_t have[MAXFRAMES];
static uint8_t sent[MAXFRAMES];
static uint8_t par[MAXFRAMES][PAYLOAD];
static uint8_t havepar[MAXFRAMES];

static int out_fd;
static struct sockaddr_in player{};
static int stride = 1;

static uint32_t queue[MAXFRAMES * 2];
static int qhead, qtail;

static void deliver(uint32_t s) {
    if (sent[s]) return;
    sent[s] = 1;
    uint8_t o[4 + PAYLOAD];
    uint32_t b = htonl(s);
    memcpy(o, &b, 4);
    memcpy(o + 4, data[s], PAYLOAD);
    sendto(out_fd, o, sizeof o, 0, (struct sockaddr *)&player, sizeof player);
}

/* a parity packet at index p covers frames p and p-stride */
static void try_par(uint32_t p) {
    if (p >= MAXFRAMES || !havepar[p]) return;
    if (p < (uint32_t)stride) return;
    uint32_t q = p - stride;
    if (have[p] && !have[q]) {
        for (int i = 0; i < PAYLOAD; i++) data[q][i] = par[p][i] ^ data[p][i];
        have[q] = 1; deliver(q); queue[qtail++] = q;
    } else if (have[q] && !have[p]) {
        for (int i = 0; i < PAYLOAD; i++) data[p][i] = par[p][i] ^ data[q][i];
        have[p] = 1; deliver(p); queue[qtail++] = p;
    }
}

/* cascade: a newly known frame can unlock parities we are already holding */
static void cascade(uint32_t s) {
    qhead = qtail = 0;
    queue[qtail++] = s;
    while (qhead < qtail) {
        uint32_t x = queue[qhead++];
        try_par(x);
        if (x + stride < MAXFRAMES) try_par(x + stride);
    }
}

int main(void) {
    const char *e;
    if ((e = getenv("FEC_STRIDE"))) stride = atoi(e);
    if (stride < 1) stride = 1;

    int in_fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in in_addr{};
    in_addr.sin_family = AF_INET;
    in_addr.sin_port = htons(47002);
    in_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    if (bind(in_fd, (struct sockaddr *)&in_addr, sizeof in_addr) < 0) {
        perror("bind 47002"); return 1;
    }
    int rcvbuf = 1 << 20;
    setsockopt(in_fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof rcvbuf);

    out_fd = socket(AF_INET, SOCK_DGRAM, 0);
    
    player.sin_family = AF_INET;
    player.sin_port = htons(47020);
    player.sin_addr.s_addr = inet_addr("127.0.0.1");

    uint8_t buf[2048];
    for (;;) {
        ssize_t n = recvfrom(in_fd, buf, sizeof buf, 0, NULL, NULL);
        if (n != PKT) continue;
        uint16_t s16;
        memcpy(&s16, buf + 1, 2);
        uint32_t s = ntohs(s16);
        if (buf[0] == 0) {
            if (have[s]) continue;              /* duplicate */
            memcpy(data[s], buf + 3, PAYLOAD);
            have[s] = 1;
            deliver(s);
            cascade(s);
        } else {
            if (havepar[s]) continue;           /* duplicate */
            memcpy(par[s], buf + 3, PAYLOAD);
            havepar[s] = 1;
            cascade(s);
        }
    }
    return 0;
}
