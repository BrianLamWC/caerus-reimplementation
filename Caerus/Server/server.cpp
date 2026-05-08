#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <cstring>
#include <fcntl.h>

#include "server.h"
#include "../proto/request.pb.h"

PeerListener::PeerListener(int listenfd, PartialSequencer* partial_sequencer, Merger* merger)
{

    args =  {listenfd, partial_sequencer, merger};
    pthread_t listener_thread;

    if (pthread_create(&listener_thread, NULL, peerListener, (void*)&args) != 0)
    {
        threadError("error creating server listener thread");
    }
    
    pthread_detach(listener_thread);

}

void *handlePeer(void *server_args)
{

    //printf("handlePeer: started\n");

    // 1) Cast and copy the entire struct
    auto* ptr = static_cast<ServerArgs*>(server_args);
    ServerArgs local = *ptr;     // copies connfd, server_addr, partial_sequencer, merger…
    free(ptr);

    // 2) Now use 'local' safely
    int connfd = local.connfd;
    sockaddr_in server_addr = local.server_addr;
    auto* partial_sequencer = local.partial_sequencer;
    auto* merger = local.merger;

    // get server's IP address and port
    char *server_ip = inet_ntoa(server_addr.sin_addr);
    int server_port = ntohs(server_addr.sin_port);

    while (true) //maintain a persistent connection
    {
        // read the 4-byte length prefix
        uint32_t netlen;
        ssize_t len = readNBytes(connfd, &netlen, sizeof(netlen));
        if (len == 0) {
            // peer cleanly closed
            break;
        }
        if (len < 0 || len != sizeof(netlen)) {
            fprintf(stderr, "PEER_HANDLER: length read error from %s:%d\n",
                    server_ip, server_port);
            break;
        }

        uint32_t msglen = ntohl(netlen);

        // read exactly msglen bytes
        std::vector<char> buf(msglen);
        ssize_t bytes_read = readNBytes(connfd, buf.data(), msglen);

        if (bytes_read != static_cast<ssize_t>(msglen)) {
            fprintf(stderr,
                    "PEER_HANDLER: Truncated read: expected %u bytes, got %zd bytes from %s:%d\n",
                    msglen,             // what we expected
                    bytes_read,         // what we actually got
                    server_ip,          // peer IP
                    server_port         // peer port
            );
            break;
        }
    
        // parse
        request::Request req_proto;
        if (!req_proto.ParseFromArray(buf.data(), msglen)) {
            fprintf(stderr, "ParseFromArray failed (%u bytes) from %s:%d\n",
                    msglen,
                    server_ip, server_port);
            break;
        }

        // Check the recipient
        if (req_proto.recipient() == request::Request::PING)
        {
            continue;
        }
        else if (req_proto.recipient() == request::Request::PARTIAL)
        {
            //printf("PARTIAL: received transaction %s from: %d\n", req_proto.transaction(0).id().c_str(), req_proto.server_id());
            partial_sequencer->pushReceivedTransactionIntoPartialSequence(req_proto);
        }
        else if (req_proto.recipient() == request::Request::MERGER)
        {


            // if partial sequence is empty, ignore
            if (req_proto.transaction_size() > 0) {
                //printf("MERGER: received partial sequence from: %d\n", req_proto.server_id());
            }
            
            {
                std::lock_guard<std::mutex> lk(partial_sequencer_to_merger_queue_mtx);
                partial_sequencer_to_merger_queue.push(req_proto);
            } // unlock first
            
            partial_sequencer_to_merger_queue_cv.notify_one();

        }
        else if(req_proto.recipient() == request::Request::START)
        {
            LOGICAL_EPOCH = std::chrono::steady_clock::now();
            LOGICAL_EPOCH_READY.store(true);
            printf("Received START from server %d, logical epoch set.\n", req_proto.server_id());
            
        }else if (req_proto.recipient() == request::Request::READY) {
            int sender_id = req_proto.server_id();  // whichever ID the other server put
            {

                printf("Received READY from server %d\n", sender_id);

                std::lock_guard<std::mutex> lg(READY_MTX);
                if (READY_SET.insert(sender_id).second) {
                    // first time seeing this server’s READY
                    READY_CV.notify_one();
                }
            }
        }else
        {
            fprintf(stderr, "Unknown recipient type: %d from %s:%d\n",
                    req_proto.recipient(), server_ip, server_port);
            break;

        }

    }
    
    close(connfd);
    return nullptr;
}

void *peerListener(void *args)
{
    PeerListenerThreadsArgs *my_args = (PeerListenerThreadsArgs *)args;
    struct sockaddr_in server_addr;
    socklen_t server_addrlen = sizeof(server_addr);

    while (true)
    {
        int connfd = accept(my_args->listenfd, (struct sockaddr *)&server_addr, &server_addrlen);

        if (connfd < 0)
        {
            if (errno == EWOULDBLOCK || errno == EAGAIN)
            {
                usleep(10000);
                continue;
            }
            else
            {
                threadError("serverListener: error accepting connection");
            }
        }

        // make accepted socket blocking so readNBytes works correctly on macOS
        int flags = fcntl(connfd, F_GETFL, 0);
        fcntl(connfd, F_SETFL, flags & ~O_NONBLOCK);

        // server args
        ServerArgs *server_args = (ServerArgs *)malloc(sizeof(ServerArgs));

        if (!server_args)
        {
            close(connfd);
            threadError("serverListener: memory allocation failed");
        }

        server_args->connfd = connfd;
        server_args->server_addr = server_addr;
        server_args->partial_sequencer = my_args->partial_sequencer;
        server_args->merger = my_args->merger;

        pthread_t server_thread;

        if (pthread_create(&server_thread, NULL, handlePeer, (void *)server_args) != 0)
        {
            free(server_args);
            close(connfd);
            threadError("serverListener: error creating thread");
        }

        pthread_detach(server_thread);
    }

    pthread_exit(NULL);
}
