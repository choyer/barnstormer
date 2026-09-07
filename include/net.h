/*
 * net.h -- networked play (planned; see doc/ROADMAP.md).
 *
 * The design is deterministic lockstep, which the simulation is already built
 * for: game_tick() consumes one control word per player and every random
 * choice runs off game->explseed / game->randseed, so two peers fed identical
 * inputs produce identical worlds.  A session therefore only ever exchanges
 * 16-bit control words plus a periodic state hash to detect divergence.
 *
 * Nothing here is implemented yet.  The header exists so that game.c's
 * input-gathering seam and the tick loop in main.c are written against the
 * final shape.
 */
#ifndef NET_H
#define NET_H

#include "sopwith.h"

/* game_t is declared in sopwith.h; net_state_hash() needs its definition,
 * so a translation unit using it must include game.h as well. */

#define NET_PROTOCOL_VERSION 1
#define NET_MAX_PEERS        MAX_PLYR
#define NET_INPUT_DELAY      3   /* ticks of lag hiding                   */

typedef struct net_session net_session_t;

typedef struct {
    uint32_t tick;
    uint16_t keys[NET_MAX_PEERS];
    uint32_t state_hash;    /* 0 when not checked this tick               */
} net_frame_t;

/* Host or join.  Returns NULL and sets errno to ENOSYS today. */
net_session_t *net_host(uint16_t port, int expected_peers);
net_session_t *net_join(const char *host, uint16_t port);
void           net_close(net_session_t *s);

/* Publish this peer's control word for `tick`, then block until every peer's
 * word for `tick - NET_INPUT_DELAY` has arrived.  Returns 0 on success. */
int  net_exchange(net_session_t *s, uint32_t tick, uint16_t keys,
                  net_frame_t *out);

int  net_local_index(const net_session_t *s);
int  net_peer_count(const net_session_t *s);

/* Rolling checksum of the simulation, for desync detection. */
uint32_t net_state_hash(const game_t *g);

#endif /* NET_H */
