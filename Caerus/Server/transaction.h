#ifndef TRANSACTION_H
#define TRANSACTION_H

#include <string>
#include <vector>
#include <cstdint>
#include <cstring>
#include <unordered_set>

enum class OperationType {
    READ,
    WRITE
};

struct Operation {
    OperationType type;
    std::string key;
    std::string value;  // Only used for write operations
};

class Transaction
{
private:
    int32_t order;
    std::string id;
    int32_t server_id;
    std::vector<Operation> operations;

    // intrusive adjacency list
    std::unordered_set<Transaction*> neighbors_out;
    std::unordered_set<Transaction*> neighbors_in;
    std::unordered_set<std::string> neighbors_in_ids;

    std::unordered_set<int32_t> expected_regions;
    std::unordered_set<int32_t> seen_regions;
public:
    Transaction(int32_t order_, int32_t server_id_, const std::vector<Operation>& ops, const std::string& id_ = "")
        :order(order_), id(id_), server_id(server_id_), operations(ops) {}


    int32_t getOrder() const { return order; }

    const std::string& getID() const { return id; }

    int32_t getServerId() const { return server_id; }

    const std::vector<Operation>& getOperations() const { return operations; }

    void addNeighborOut(Transaction* ptr) { 
        neighbors_out.insert(ptr); 
        ptr->neighbors_in.insert(this); 
    }

    void addNeighborInID(const std::string& neighbor_id) {
        neighbors_in_ids.insert(neighbor_id);
    }

    void removeOutNeighbor(Transaction* ptr) { 
        neighbors_out.erase(ptr); 
        ptr->neighbors_in.erase(this);
    }

    void removeInNeighbor(Transaction* ptr) { 
        neighbors_in.erase(ptr); 
        ptr->neighbors_out.erase(this);
    }

    const std::unordered_set<Transaction*>& getOutNeighbors() const { return neighbors_out; }

    const std::unordered_set<Transaction*>& getIncomingNeighbors() const { return neighbors_in; }

    const std::unordered_set<std::string>& getIncomingNeighborIDs() const { return neighbors_in_ids; }

    void setExpectedRegions(const std::unordered_set<int32_t>& regions) { expected_regions = regions; }
    const std::unordered_set<int32_t>& getExpectedRegions() const { return expected_regions; }

    void addSeenRegion(int32_t region) { seen_regions.insert(region); }
    const std::unordered_set<int32_t>& getSeenRegions() const { return seen_regions; }

    bool isComplete() const { return seen_regions == expected_regions; }


    
};

#endif
