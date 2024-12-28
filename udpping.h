/*
** UDP Ping server communication
*/
#ifndef UDPPING_H
#define UDPPING_H

enum e_ping_type {
    PT_PING_REQUEST,
    PT_PING_REPLY,
    PT_PING_STATS
};

union u_pt_info {
    struct s_pt_ping {
        int delay;
    } pt_ping;
    struct s_pt_stats {
        int max_delay;
        int latency_median;
        int latency_low;
        int latency_high;
        int reliability;
    } pt_stats;
};

struct s_ping_packet {
    enum e_ping_type type;
    union u_pt_info info;
    int uuid[4];
};

#endif