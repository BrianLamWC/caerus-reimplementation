#include <vector>
#include "queue_ts.h"

// Global instantiations:
QueueTS<request::Request> request_queue;

QueueTS<request::Request> batcher_to_partial_sequencer_queue;

QueueTS<request::Request> partial_sequencer_to_merger_queue;
std::mutex partial_sequencer_to_merger_queue_mtx;
std::condition_variable partial_sequencer_to_merger_queue_cv;

template<typename T>
void QueueTS<T>::push(const T& val) {
    std::lock_guard<std::mutex> lock(mtx);
    q.push_back(val);  // use push_back for deque
}

template<typename T>
void QueueTS<T>::pushAll(const std::vector<T>& items) {
    std::lock_guard<std::mutex> lock(mtx);
    for (const auto &v : items) {
      q.push_back(v);
    }
}

template <typename T>
bool QueueTS<T>::empty() {
    std::lock_guard<std::mutex> lock(mtx);
    return q.empty();
}

template<typename T>
std::vector<T> QueueTS<T>::popAll() {
    std::lock_guard<std::mutex> lock(mtx);
    std::vector<T> items;
    while (!q.empty()) {
        items.push_back(q.front());
        q.pop_front();  // use pop_front for deque
    }
    return items;
}

template <typename T>
T QueueTS<T>::pop() {
    std::lock_guard<std::mutex> lock(mtx);
    if (q.empty()) {
        throw std::runtime_error("pop() called on empty queue");
    }
    T item = std::move(q.front());
    q.pop_front();  // use pop_front for deque
    return item;
}

template <typename T>
std::vector<T> QueueTS<T>::snapshot() const {
    std::lock_guard<std::mutex> lock(mtx);   
    return std::vector<T>(q.begin(), q.end()); 
}

// Explicit template instantiations:
template class QueueTS<request::Transaction>;
template class QueueTS<Transaction>;
template class QueueTS<std::vector<Transaction>>;

template class QueueTS<std::vector<request::Request>>;
template class QueueTS<request::Request>;
