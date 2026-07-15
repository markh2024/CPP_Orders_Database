#include "AuthManager.h"
#include "picosha2.h"
#include <fstream>
#include <sstream>
#include <random>

AuthManager::AuthManager(std::string usersPath) : path_(std::move(usersPath)) {}

std::string AuthManager::randomSalt() {
    static const char hexChars[] = "0123456789abcdef";
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dist(0, 15);
    std::string salt;
    salt.reserve(32);
    for (int i = 0; i < 32; ++i) salt += hexChars[dist(gen)];
    return salt;
}

std::string AuthManager::hashPassword(const std::string& salt, const std::string& password) {
    return picosha2::hash256_hex_string(salt + password);
}

bool AuthManager::load() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ifstream in(path_);
    if (!in.is_open()) return true; // no users yet, fine on first run
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        std::istringstream iss(line);
        std::string user, salt, hash, adminFlag;
        std::getline(iss, user, '|');
        std::getline(iss, salt, '|');
        std::getline(iss, hash, '|');
        std::getline(iss, adminFlag, '|');
        // Later lines for the same username win (e.g. after a password
        // change), same append-only convention used for orders.dat.
        if (!user.empty()) users_[user] = {salt, hash, adminFlag == "1"};
    }
    return true;
}

void AuthManager::appendUserLine(const std::string& username, const Rec& rec) {
    std::ofstream out(path_, std::ios::app);
    out << username << "|" << rec.salt << "|" << rec.hash << "|" << (rec.isAdmin ? "1" : "0") << "\n";
}

bool AuthManager::createUser(const std::string& username, const std::string& password, bool isAdmin) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (users_.count(username)) return false; // already exists
    Rec rec{randomSalt(), "", isAdmin};
    rec.hash = hashPassword(rec.salt, password);
    users_[username] = rec;
    appendUserLine(username, rec);
    return true;
}

bool AuthManager::verify(const std::string& username, const std::string& password) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = users_.find(username);
    if (it == users_.end()) return false;
    return hashPassword(it->second.salt, password) == it->second.hash;
}

bool AuthManager::userExists(const std::string& username) {
    std::lock_guard<std::mutex> lock(mutex_);
    return users_.count(username) > 0;
}

bool AuthManager::isAdmin(const std::string& username) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = users_.find(username);
    return it != users_.end() && it->second.isAdmin;
}

bool AuthManager::changePassword(const std::string& username, const std::string& oldPassword,
                                  const std::string& newPassword) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = users_.find(username);
    if (it == users_.end()) return false;
    if (hashPassword(it->second.salt, oldPassword) != it->second.hash) return false; // wrong old password

    Rec rec{randomSalt(), "", it->second.isAdmin};
    rec.hash = hashPassword(rec.salt, newPassword);
    users_[username] = rec;
    appendUserLine(username, rec); // append-only: this newer line wins on next load()
    return true;
}

bool AuthManager::resetPassword(const std::string& username, const std::string& newPassword) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = users_.find(username);
    if (it == users_.end()) return false;

    Rec rec{randomSalt(), "", it->second.isAdmin};
    rec.hash = hashPassword(rec.salt, newPassword);
    users_[username] = rec;
    appendUserLine(username, rec);
    return true;
}
