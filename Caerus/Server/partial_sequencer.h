#ifndef PARTIAL_SEQUENCER_H
#define PARTIAL_SEQUENCER_H

#include <vector>   

#include "transaction.h"
#include "queue_ts.h"
#include "../proto/request.pb.h"
#include "utils.h"

class PartialSequencer
{
private:

    int32_t next_round{0};
    
    std::vector<Transaction> partial_sequence;
    std::vector<request::Request> transactions_received;
    request::Request partial_sequence_proto;
    pthread_t partial_sequencer_thread;
        
    std::unordered_map<int, ServerInfo> target_peers;
    std::unordered_map<int,int> merger_fds; // id to fd mapping for merger

public:
    PartialSequencer();
    void processPartialSequence();
    void pushReceivedTransactionIntoPartialSequence(const request::Request& req_proto);
    void sendPartialSequence();
};

#endif
