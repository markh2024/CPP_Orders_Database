#pragma once
#include <string>
#include <unordered_map>
#include <mutex>

// Stores users as: username|salt|hash|isAdmin  (one per line, isAdmin is 0/1)
// Password hash = SHA256(salt + password), salt is a random hex string
// generated per user. This is adequate for a small-office internal tool;
// for anything internet-facing, swap in a slow KDF like bcrypt/Argon2.
class AuthManager {
public:
    explicit AuthManager(std::string usersPath);

    bool load();
    bool createUser(const std::string& username, const std::string& password, bool isAdmin = false);
    bool verify(const std::string& username, const std::string& password);
    bool userExists(const std::string& username);
    bool isAdmin(const std::string& username);

    // Self-service change: caller must supply the correct current password.
    bool changePassword(const std::string& username, const std::string& oldPassword,
                         const std::string& newPassword);
    // Admin override: no old password required (for resetting a locked-out user).
    bool resetPassword(const std::string& username, const std::string& newPassword);

private:
    std::string path_;
    std::mutex mutex_;
    struct Rec { std::string salt, hash; bool isAdmin = false; };
    std::unordered_map<std::string, Rec> users_;

    static std::string randomSalt();
    static std::string hashPassword(const std::string& salt, const std::string& password);
    void appendUserLine(const std::string& username, const Rec& rec);
};
