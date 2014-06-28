#ifndef STATS_H
#define STATS_H

void *stats_connection(int sd, void *ctx);
int stats_recv(int sd, void *data, void *ctx);

#endif
