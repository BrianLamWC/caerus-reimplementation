#ifndef SERVER_H
#define SERVER_H

#include <string>
#include <vector>
#include <netinet/in.h>

#include "utils.h"
#include "partial_sequencer.h"
#include "merger.h"

struct PeerListenerThreadsArgs
{
    int listenfd;
    PartialSequencer* partial_sequencer;
    Merger* merger;
};

struct ServerArgs
{
    int connfd;
    struct sockaddr_in server_addr;
    PartialSequencer* partial_sequencer;
    Merger* merger;

};


void* handlePeer(void *server_args);
void* peerListener(void *args);

class PeerListener
{
private:
    PeerListenerThreadsArgs args;
public:

    PeerListener(int listenfd, PartialSequencer* partial_sequencer, Merger* merger);

};

#endif // SERVER_H
