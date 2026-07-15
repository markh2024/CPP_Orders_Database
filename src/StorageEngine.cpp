#include "StorageEngine.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <iostream>

StorageEngine::StorageEngine(std::string dataPath) : path_(std::move(dataPath)) {}

bool StorageEngine::load() {
    std::lock_guard<std::mutex> lock(mutex_);

    // Open for read+write, create if missing.
    file_.open(path_, std::ios::in | std::ios::out | std::ios::app);
    if (!file_.is_open()) {
        // File may not exist yet; create it then reopen for read/write.
        std::ofstream create(path_, std::ios::app);
        create.close();
        file_.open(path_, std::ios::in | std::ios::out);
        if (!file_.is_open()) return false;
    }

    file_.clear();
    file_.seekg(0);
    std::string line;
    std::streamoff offset = file_.tellg();
    while (std::getline(file_, line)) {
        if (!line.empty()) {
            if (line[0] == 'D' && line.size() > 2 && line[1] == FIELD_DELIM) {
                // tombstone line: "D|<id>"
                long id = std::stol(line.substr(2));
                deletedIds_.insert(id);
                idToOffset_.erase(id);
            } else {
                Order o = Order::deserialize(line);
                if (o.id >= 0) {
                    idToOffset_[o.id] = offset;
                    if (o.id >= nextId_) nextId_ = o.id + 1;
                }
            }
        }
        offset = file_.tellg();
    }
    file_.clear(); // clear eof

    // Rebuild secondary indexes from the latest surviving version of each id.
    for (auto& [id, off] : idToOffset_) {
        if (deletedIds_.count(id)) continue;
        Order o = readAt(off);
        indexOrder(o, off);
    }
    return true;
}

std::vector<std::string> StorageEngine::tokenize(const std::string& text) {
    std::vector<std::string> words;
    std::string cur;
    for (char c : text) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            cur += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        } else if (!cur.empty()) {
            words.push_back(cur);
            cur.clear();
        }
    }
    if (!cur.empty()) words.push_back(cur);
    return words;
}

void StorageEngine::indexOrder(const Order& o, std::streamoff offset) {
    idToOffset_[o.id] = offset;
    deletedIds_.erase(o.id);
    byDate_[o.dateOrdered].insert(o.id);

    std::string supLower = o.supplier;
    std::transform(supLower.begin(), supLower.end(), supLower.begin(), ::tolower);
    bySupplier_[supLower].insert(o.id);

    std::string whoLower = o.orderedBy;
    std::transform(whoLower.begin(), whoLower.end(), whoLower.begin(), ::tolower);
    byOrderedBy_[whoLower].insert(o.id);

    for (auto& w : tokenize(o.description)) byDescWord_[w].insert(o.id);
    for (auto& w : tokenize(o.partNumber)) byPartWord_[w].insert(o.id);

    std::string contactLower = o.contactName;
    std::transform(contactLower.begin(), contactLower.end(), contactLower.begin(), ::tolower);
    byContactName_[contactLower].insert(o.id);

    byContactNumber_[o.contactNumber].insert(o.id);

    std::string clientLower = o.clientName;
    std::transform(clientLower.begin(), clientLower.end(), clientLower.begin(), ::tolower);
    byClientName_[clientLower].insert(o.id);
}

void StorageEngine::deindexOrder(const Order& o) {
    byDate_[o.dateOrdered].erase(o.id);
    std::string supLower = o.supplier;
    std::transform(supLower.begin(), supLower.end(), supLower.begin(), ::tolower);
    bySupplier_[supLower].erase(o.id);
    std::string whoLower = o.orderedBy;
    std::transform(whoLower.begin(), whoLower.end(), whoLower.begin(), ::tolower);
    byOrderedBy_[whoLower].erase(o.id);
    for (auto& w : tokenize(o.description)) byDescWord_[w].erase(o.id);
    for (auto& w : tokenize(o.partNumber)) byPartWord_[w].erase(o.id);

    std::string contactLower = o.contactName;
    std::transform(contactLower.begin(), contactLower.end(), contactLower.begin(), ::tolower);
    byContactName_[contactLower].erase(o.id);

    byContactNumber_[o.contactNumber].erase(o.id);

    std::string clientLower = o.clientName;
    std::transform(clientLower.begin(), clientLower.end(), clientLower.begin(), ::tolower);
    byClientName_[clientLower].erase(o.id);
}

void StorageEngine::appendLine(const std::string& line, std::streamoff& outOffset) {
    file_.clear();
    file_.seekp(0, std::ios::end);
    outOffset = file_.tellp();
    file_ << line << "\n";
    file_.flush();
}

Order StorageEngine::readAt(std::streamoff offset) {
    file_.clear();
    file_.seekg(offset);
    std::string line;
    std::getline(file_, line);
    return Order::deserialize(line);
}

long StorageEngine::addOrder(Order o)
{
    std::lock_guard<std::mutex> lock(mutex_);

    // Assign new unique ID
    o.id = nextId_++;
    // Calculate:
    // subtotal = quantity * unitPrice
    // total    = subtotal
    o.calculateTotals();
    std::streamoff offset;
    appendLine(
        o.serialize(),
        offset
    );
    indexOrder(
        o,
        offset
    );
    return o.id;
}

bool StorageEngine::updateOrder(const Order& o)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = idToOffset_.find(o.id);

    if (it == idToOffset_.end())
        return false;
    // Make a writable copy because the input object is const
    Order updated = o;
    // Recalculate:
    // subtotal = quantity * unitPrice
    // total    = subtotal
    updated.calculateTotals();
    // Remove previous index entries
    Order old = readAt(it->second);
    deindexOrder(old);
    // Append new version
    std::streamoff offset;
    appendLine(
        updated.serialize(),
        offset
    );
    // Rebuild indexes using updated record
    indexOrder(
        updated,
        offset
    );
    return true;
}

std::vector<Order> StorageEngine::advancedSearch(
    const std::string& startDate,
    const std::string& endDate,
    const std::string& customer,
    const std::string& supplier,
    const std::string& partNumber,
    const std::string& orderedBy,
    const std::string& contactNumber,
    const std::string& clientName)
{
    std::lock_guard<std::mutex> lock(mutex_);


    std::vector<Order> result;



    for (auto& [id, offset] : idToOffset_)
    {
        Order o = readAt(offset);


        bool match = true;



        // Date range

        if (!startDate.empty())
        {
            if (o.dateOrdered < startDate)
                match = false;
        }


        if (!endDate.empty())
        {
            if (o.dateOrdered > endDate)
                match = false;
        }



        // Customer

        if (!customer.empty())
        {
            std::string a = o.customer;
            std::string b = customer;

            std::transform(
                a.begin(),
                a.end(),
                a.begin(),
                ::tolower
            );

            std::transform(
                b.begin(),
                b.end(),
                b.begin(),
                ::tolower
            );


            if (a.find(b) == std::string::npos)
                match = false;
        }



        // Supplier

        if (!supplier.empty())
        {
            std::string a = o.supplier;
            std::string b = supplier;

            std::transform(
                a.begin(),
                a.end(),
                a.begin(),
                ::tolower
            );

            std::transform(
                b.begin(),
                b.end(),
                b.begin(),
                ::tolower
            );


            if (a.find(b) == std::string::npos)
                match = false;
        }




        // Part number

        if (!partNumber.empty())
        {
            std::string a = o.partNumber;
            std::string b = partNumber;


            std::transform(
                a.begin(),
                a.end(),
                a.begin(),
                ::tolower
            );


            std::transform(
                b.begin(),
                b.end(),
                b.begin(),
                ::tolower
            );


            if (a.find(b) == std::string::npos)
                match = false;
        }





        // Ordered By

        if (!orderedBy.empty())
        {
            std::string a = o.orderedBy;
            std::string b = orderedBy;


            std::transform(
                a.begin(),
                a.end(),
                a.begin(),
                ::tolower
            );


            std::transform(
                b.begin(),
                b.end(),
                b.begin(),
                ::tolower
            );


            if (a.find(b) == std::string::npos)
                match = false;
        }




        // Contact number

        if (!contactNumber.empty())
        {
            if (o.contactNumber.find(contactNumber)
                == std::string::npos)
            {
                match = false;
            }
        }



        // Client name

        if (!clientName.empty())
        {
            std::string a = o.clientName;
            std::string b = clientName;

            std::transform(a.begin(), a.end(), a.begin(), ::tolower);
            std::transform(b.begin(), b.end(), b.begin(), ::tolower);

            if (a.find(b) == std::string::npos)
                match = false;
        }




        if (match)
        {
            result.push_back(o);
        }
    }



    return result;
}

bool StorageEngine::deleteOrder(long id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = idToOffset_.find(id);
    if (it == idToOffset_.end()) return false;
    Order old = readAt(it->second);
    deindexOrder(old);
    idToOffset_.erase(it);
    deletedIds_.insert(id);
    std::streamoff offset;
    appendLine("D" + std::string(1, FIELD_DELIM) + std::to_string(id), offset);
    return true;
}

std::optional<Order> StorageEngine::findById(long id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = idToOffset_.find(id);
    if (it == idToOffset_.end()) return std::nullopt;
    return readAt(it->second);
}

std::vector<Order> StorageEngine::searchByDescription(const std::string& keyword) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> words = tokenize(keyword);
    std::set<long> matches;

    // Pass 1: exact-word index lookup (fast path, O(query words)).
    // Any index word that starts with or contains the query word counts,
    // so a search for "bolt" also matches the indexed token "bolts".
    for (auto& w : words) {
        for (auto& [indexWord, ids] : byDescWord_) {
            if (indexWord.find(w) != std::string::npos) {
                matches.insert(ids.begin(), ids.end());
            }
        }
    }

    std::vector<Order> result;
    for (long id : matches) result.push_back(readAt(idToOffset_[id]));
    return result;
}

std::vector<Order> StorageEngine::searchByPartNumber(const std::string& keyword) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> words = tokenize(keyword);
    std::set<long> matches;
    for (auto& w : words) {
        for (auto& [indexWord, ids] : byPartWord_) {
            if (indexWord.find(w) != std::string::npos) {
                matches.insert(ids.begin(), ids.end());
            }
        }
    }
    std::vector<Order> result;
    for (long id : matches) result.push_back(readAt(idToOffset_[id]));
    return result;
}

std::vector<Order> StorageEngine::searchByContactName(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string key = name;
    std::transform(key.begin(), key.end(), key.begin(), ::tolower);
    std::set<long> matches;
    // Substring match over contact names - typically a handful of distinct
    // contacts per supplier, so a scan here is cheap.
    for (auto& [contact, ids] : byContactName_) {
        if (contact.find(key) != std::string::npos) matches.insert(ids.begin(), ids.end());
    }
    std::vector<Order> result;
    for (long id : matches) result.push_back(readAt(idToOffset_[id]));
    return result;
}

std::vector<Order> StorageEngine::searchByContactNumber(const std::string& number) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::set<long> matches;
    // Substring match so a partial number (e.g. last 4 digits) still finds it.
    for (auto& [num, ids] : byContactNumber_) {
        if (num.find(number) != std::string::npos) matches.insert(ids.begin(), ids.end());
    }
    std::vector<Order> result;
    for (long id : matches) result.push_back(readAt(idToOffset_[id]));
    return result;
}

std::vector<Order> StorageEngine::searchByClientName(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string key = name;
    std::transform(key.begin(), key.end(), key.begin(), ::tolower);
    std::set<long> matches;
    for (auto& [client, ids] : byClientName_) {
        if (client.find(key) != std::string::npos) matches.insert(ids.begin(), ids.end());
    }
    std::vector<Order> result;
    for (long id : matches) result.push_back(readAt(idToOffset_[id]));
    return result;
}

std::vector<Order> StorageEngine::searchBySupplier(const std::string& supplier) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string key = supplier;
    std::transform(key.begin(), key.end(), key.begin(), ::tolower);
    std::vector<Order> result;
    auto it = bySupplier_.find(key);
    if (it != bySupplier_.end())
        for (long id : it->second) result.push_back(readAt(idToOffset_[id]));
    return result;
}

std::vector<Order> StorageEngine::searchByOrderedBy(const std::string& username) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string key = username;
    std::transform(key.begin(), key.end(), key.begin(), ::tolower);
    std::vector<Order> result;
    auto it = byOrderedBy_.find(key);
    if (it != byOrderedBy_.end())
        for (long id : it->second) result.push_back(readAt(idToOffset_[id]));
    return result;
}

std::vector<Order> StorageEngine::searchByDateRange(const std::string& startDate,
                                                     const std::string& endDate) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Order> result;
    // byDate_ is a std::map, so lower_bound/upper_bound give us the range
    // directly since ISO dates (YYYY-MM-DD) sort correctly as strings.
    auto lo = byDate_.lower_bound(startDate);
    auto hi = byDate_.upper_bound(endDate);
    for (auto it = lo; it != hi; ++it)
        for (long id : it->second) result.push_back(readAt(idToOffset_[id]));
    return result;
}

std::vector<Order> StorageEngine::all() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Order> result;
    for (auto& [id, off] : idToOffset_) result.push_back(readAt(off));
    return result;
}

std::vector<Order> StorageEngine::searchByMonth(int year, int month) {
    if (month < 1 || month > 12) return {};

    auto isLeapYear = [](int y) {
        return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
    };
    static const int daysInMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int lastDay = daysInMonth[month - 1];
    if (month == 2 && isLeapYear(year)) lastDay = 29;

    char startBuf[11], endBuf[11];
    std::snprintf(startBuf, sizeof(startBuf), "%04d-%02d-%02d", year, month, 1);
    std::snprintf(endBuf, sizeof(endBuf), "%04d-%02d-%02d", year, month, lastDay);

    return searchByDateRange(startBuf, endBuf);
}
