#ifndef QUEUE_TS_H
#define QUEUE_TS_H

#include <mutex>
#include <deque>
#include <condition_variable>

#include "transaction.h"
#include "../proto/request.pb.h"

// QUEUES
template<typename T>
class QueueTS
{
private:

    std::deque<T> q;
    mutable std::mutex mtx;

public:

    void push(const T& val);
    void pushAll(const std::vector<T>& items);
    bool empty();
    std::vector<T> popAll(); 
    T pop(); 
    std::vector<T> snapshot() const;
    size_t size() {
        std::lock_guard<std::mutex> lock(mtx);
        return q.size();
    }
};

// queue for client requests to batcher
extern QueueTS<request::Request> request_queue;

// queue for batcher to partial sequencer
extern QueueTS<request::Request> batcher_to_partial_sequencer_queue;

// queue for partial sequencer to merger
extern QueueTS<request::Request> partial_sequencer_to_merger_queue;
extern std::mutex partial_sequencer_to_merger_queue_mtx;
extern std::condition_variable partial_sequencer_to_merger_queue_cv;


#endif // QUEUE_TS_H
