#include <unistd.h>
#include <pthread.h>
#include <iostream>

#include "server.h"
#include "client.h"
#include "utils.h"
#include "batcher.h"
#include "partial_sequencer.h"
#include "merger.h"
#include "graph.h"
#include "logger.h"


int main(int argc, char *argv[])
{
    if (argc < 2 || argc > 3)
    {
        std::cerr << "Usage: " << argv[0] << " <id> [local]" << std::endl;
        return 1;
    }

    bool local_mode = false;
    if (argc == 3)
    {
        if (std::string(argv[2]) != "local")
        {
            std::cerr << "Usage: " << argv[0] << " <id> [local]" << std::endl;
            return 1;
        }
        local_mode = true;
    }

    signal(SIGPIPE, SIG_IGN);

    peer_port = 8001;
    int client_port = 7001;
    my_id = std::stoi(argv[1]);

    // setup mockdb
    setupMockDB();

    // get list of servers
    getServers(local_mode);

    // in local mode, override ports from this node's config entry
    if (local_mode)
    {
        bool found = false;
        for (const auto& s : servers)
        {
            if (s.id == my_id)
            {
                peer_port = s.port;
                client_port = s.client_port;
                found = true;
                break;
            }
        }
        if (!found)
        {
            std::cerr << "Error: no entry for id " << my_id << " in local_servers.json\n";
            return 1;
        }
    }
    int num_servers = servers.size();

    

    // run batcher
    Batcher batcher;

    // run partial sequencer
    PartialSequencer partial_sequencer;

    // run merger
    Merger merger;
    
    // run logger
    //Logger logger;

    // set up listening sockets
    int peer_listenfd = setupListenfd(peer_port);
    int client_listenfd = setupListenfd(client_port);

    // listening for incoming connections
    listen(peer_listenfd, 5);
    listen(client_listenfd, 5);

    printf("Listening for peers on port %d\n", peer_port);
    printf("Listening for clients on port %d\n", client_port);

    // start listeners
    PeerListener peer_listener(peer_listenfd, &partial_sequencer, &merger);
    ClientListener client_listener(client_listenfd, &merger);

    Coordinator coordinator;

    // arguments for pinger thread
    //Pinger pinger(&servers, num_servers, peer_port);

    while (true) {
        pause();
    }


    close(peer_listenfd);
    close(client_listenfd);

    return 0;
}
