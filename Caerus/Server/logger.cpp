#include <fstream>
#include <thread>

#include "logger.h"
#include "graph.h"
#include "json.hpp"
using json = nlohmann::json;

QueueTS<Transaction> merged_order;
std::mutex logging_mutex;
std::condition_variable logging_cv;

void Logger::dumpDB() {
    namespace fs = std::filesystem;
    fs::create_directories("./logs");
    std::ofstream ofs("./db_state/db_state" + std::to_string(my_id) + ".json",
                      std::ios::out|std::ios::trunc);
    // give it a big buffer so that each << doesn't flush
    static char buf[1<<20];
    ofs.rdbuf()->pubsetbuf(buf, sizeof(buf));

    ofs << "{\"data_items\":[\n";
    bool first = true;
    for (auto& [key,item] : mock_db_logging) {
        if (!first) ofs << ",\n";
        first = false;
        ofs
          << "  {\"key\":\""     << key
          << "\",\"value\":\""   << item.val
          << "\",\"primary_server_id\":" << item.primary_copy_id
          << "}";
    }
    ofs << "\n]}\n";
}


void Logger::logMergedOrders()
{

    // track how many transactions we have logged
    int32_t logged_txns = 0;

    while (true)
    {

        std::vector<Transaction> txns;
        {
            std::unique_lock<std::mutex> lock(logging_mutex);
            logging_cv.wait(
                lock,
                [this] { return shutdown || !merged_order.empty(); }
            );
            txns = merged_order.popAll(); 
            logged_txns += txns.size();
        }

        if (shutdown)
        {
            std::cout << "Logger: Shutting down logging thread.\n";
            break; // exit the thread
        }

        if (txns.empty())
        {
            std::cout << "Logger: No transactions to log.\n";
            continue; // nothing to log
        }

        // apply changes to mock_db_logging
        
        for(auto & txn : txns){

            for (auto & op : txn.getOperations())
            {

                if (op.type == OperationType::WRITE) {
                    // write operation
                    auto it = mock_db_logging.find(op.key);
                    if (it != mock_db_logging.end()) {
                        it->second.val = op.value; // update value
                    }

                } else if (op.type == OperationType::READ) {
                    // read operation, we can log or ignore it as needed
                    auto it = mock_db_logging.find(op.key);
                    if (it != mock_db_logging.end()) {
                        // log read operation if necessary
                    }
                }

            }

        }
        

    }

}

Logger::Logger(){
    shutdown = false;
    if (pthread_create(&logging_thread, NULL, [](void* arg) -> void* {
            static_cast<Logger*>(arg)->Logger::logMergedOrders();
            return nullptr;
        }, this) != 0) 
    {
        threadError("Error creating sender thread");
    }

    std::cout << "Logger: Logging thread created.\n";

    pthread_detach(logging_thread);

}

Logger::~Logger() {
    {
        std::lock_guard<std::mutex> lk(logging_mutex);
        shutdown = true;
    }
    
    logging_cv.notify_all();

}

