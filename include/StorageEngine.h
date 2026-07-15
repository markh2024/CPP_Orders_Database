#pragma once
#include "Order.h"
#include <string>
#include <unordered_map>
#include <map>
#include <set>
#include <vector>
#include <mutex>
#include <fstream>
#include <optional>

// StorageEngine owns the on-disk order file and rebuilds/maintains
// in-memory indexes for fast lookup. The on-disk format is append-only:
// updates and deletes are appended as new lines rather than rewriting
// the file in place, which keeps writes cheap and crash-safe (no partial
// rewrites). Indexes are derived and rebuilt from the file at startup.
class StorageEngine {
public:
    explicit StorageEngine(std::string dataPath);

    bool load();                       // read existing file, build indexes
    long addOrder(Order o);            // assigns id, returns it
    bool updateOrder(const Order& o);  // o.id must exist
    bool deleteOrder(long id);
    std::optional<Order> findById(long id);
    // -----------------------------------------
    // Basic searches
    // -----------------------------------------

    std::vector<Order> searchByDescription(const std::string& keyword);
    std::vector<Order> searchByPartNumber(const std::string& keyword);
    std::vector<Order> searchBySupplier(const std::string& supplier);
    std::vector<Order> searchByOrderedBy(const std::string& username);
    std::vector<Order> searchByContactName(const std::string& name);
    std::vector<Order> searchByContactNumber(const std::string& number);
    std::vector<Order> searchByClientName(const std::string& name);
    std::vector<Order> searchByDateRange(const std::string& startDate,
                                          const std::string& endDate);
    std::vector<Order> searchByMonth(int year, int month); // e.g. (2026, 7) for July 2026



    // -----------------------------------------
    // Advanced combined search
    // -----------------------------------------

    std::vector<Order> advancedSearch(
        const std::string& startDate,
        const std::string& endDate,
        const std::string& customer,
        const std::string& supplier,
        const std::string& partNumber,
        const std::string& orderedBy,
        const std::string& contactNumber,
        const std::string& clientName
    );



    std::vector<Order> all();

private:
    std::string path_;
    std::fstream file_;
    std::mutex mutex_;
    long nextId_ = 1;

    std::unordered_map<long, std::streamoff> idToOffset_;
    std::set<long> deletedIds_;

    // secondary indexes: key -> set of order ids
    std::map<std::string, std::set<long>> byDate_;
    std::unordered_map<std::string, std::set<long>> bySupplier_;
    std::unordered_map<std::string, std::set<long>> byOrderedBy_;
    std::unordered_map<std::string, std::set<long>> byDescWord_;
    std::unordered_map<std::string, std::set<long>> byPartWord_;
    std::unordered_map<std::string, std::set<long>> byContactName_;
    std::unordered_map<std::string, std::set<long>> byContactNumber_;
    std::unordered_map<std::string, std::set<long>> byClientName_;

    void indexOrder(const Order& o, std::streamoff offset);
    void deindexOrder(const Order& o);
    void appendLine(const std::string& line, std::streamoff& outOffset);
    std::vector<std::string> tokenize(const std::string& text);
    Order readAt(std::streamoff offset);
};
