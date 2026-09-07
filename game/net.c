/*
 * net.c -- networked play.
 *
 * Not implemented; see doc/ROADMAP.md for the lockstep design and net.h for
 * the interface it will fill in.  The stubs exist so the header is compiled
 * and callers can be written against the final API today.
 */
#include <errno.h>

#include "game.h"
#include "net.h"

net_session_t *net_host(uint16_t port, int expected_peers)
{
    (void)port; (void)expected_peers;
    errno = ENOSYS;
    return NULL;
}

net_session_t *net_join(const char *host, uint16_t port)
{
    (void)host; (void)port;
    errno = ENOSYS;
    return NULL;
}

void net_close(net_session_t *s) { (void)s; }

int net_exchange(net_session_t *s, uint32_t tick, uint16_t keys,
                 net_frame_t *out)
{
    (void)s; (void)tick; (void)keys; (void)out;
    errno = ENOSYS;
    return -1;
}

int net_local_index(const net_session_t *s) { (void)s; return 0; }
int net_peer_count(const net_session_t *s) { (void)s; return 1; }

/* This one is real: it is what the soak test's determinism check and the
 * future desync detector both need, and it costs nothing to provide now. */
uint32_t net_state_hash(const game_t *g)
{
    uint32_t h = 2166136261u;
#define MIX(v) do { h ^= (uint32_t)(v); h *= 16777619u; } while (0)
    for (const object_t *ob = g->top; ob; ob = ob->next) {
        MIX(ob->type); MIX(ob->state); MIX(ob->x); MIX(ob->y);
        MIX(ob->dx); MIX(ob->dy); MIX(ob->lx); MIX(ob->ly);
        MIX(ob->angle); MIX(ob->orient); MIX(ob->speed); MIX(ob->life);
    }
    for (int i = 0; i < MAX_X; i++)
        MIX(g->ground[i]);
    MIX(g->countmove);
    MIX(g->explseed);
    for (int i = 0; i < MAX_PLYR; i++)
        MIX(g->pool[i].score);
#undef MIX
    return h;
}
