#include "Socket.h"
#include "Protocol.h"
#include "StorageEngine.h"
#include "AuthManager.h"
#include <iostream>
#include <fstream>
#include <thread>
#include <chrono>
#include <atomic>
#include <mutex>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <sstream>

static StorageEngine* g_storage = nullptr;
static AuthManager* g_auth = nullptr;

constexpr int MAX_CONNECTIONS = 5;
static std::atomic<int> g_activeConnections{0};
static std::mutex g_logMutex; // guards both console and file logging below

static std::string timestampNow() {
    auto t = std::time(nullptr);
    std::tm tmBuf{};
#ifdef _WIN32
    localtime_s(&tmBuf, &t);
#else
    localtime_r(&t, &tmBuf);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tmBuf, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

// Just the date portion (YYYY-MM-DD), used to default a blank start/end date
// in date-range searches to "today" rather than leaving it unfiltered.
static std::string todayDateString() {
    auto t = std::time(nullptr);
    std::tm tmBuf{};
#ifdef _WIN32
    localtime_s(&tmBuf, &t);
#else
    localtime_r(&t, &tmBuf);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tmBuf, "%Y-%m-%d");
    return oss.str();
}

// Logs to both the console and data/connections.log, so there's a
// persistent record on disk as well as visibility while the server is
// running in a foreground terminal.
static void logConnectionEvent(const std::string& message) {
    std::lock_guard<std::mutex> lock(g_logMutex);
    std::string line = "[" + timestampNow() + "] " + message;
    std::cout << line << "\n";
    std::ofstream log("data/connections.log", std::ios::app);
    if (log.is_open()) log << line << "\n";
}

static void sendRow(const Socket& s, const Order& o) {
    s.sendLine(joinFields({"ROW", std::to_string(o.id), o.customer, o.supplier,
                           o.description, o.partNumber, o.website, o.contactName,
                           o.contactNumber, o.orderedBy, o.dateOrdered, o.status,
                           std::to_string(o.total), o.clientName, std::to_string(o.quantity)}));
}

static void sendRows(const Socket& s, const std::vector<Order>& orders) {
    for (auto& o : orders) sendRow(s, o);
    s.sendLine("END");
}

// Blank or unparseable quantity defaults to 1, per the requirement that a
// left-blank quantity should mean "one", not zero or a parse error.
static int parseQuantity(const std::string& s) {
    if (s.empty()) return 1;
    try {
        int q = std::stoi(s);
        return q > 0 ? q : 1;
    } catch (...) {
        return 1;
    }
}

static void handleClient(Socket sock) {
    bool loggedIn = false;
    std::string currentUser;
    std::string line;
    std::string peerAddr = sock.peerAddress();

    while (sock.recvLine(line)) {
        auto f = splitFields(line);
        if (f.empty() || f[0].empty()) continue;
        const std::string& cmd = f[0];

        if (cmd == "LOGIN") {
            if (f.size() < 3) { sock.sendLine("ERR|malformed LOGIN"); continue; }
            if (g_auth->verify(f[1], f[2])) {
                loggedIn = true;
                currentUser = f[1];
                sock.sendLine("OK|welcome " + currentUser);
            } else {
                sock.sendLine("ERR|invalid credentials");
            }
            continue;
        }

        if (cmd == "QUIT") { sock.sendLine("OK|bye"); break; }

        if (!loggedIn) { sock.sendLine("ERR|not logged in"); continue; }

        if (cmd == "REGISTER") {
            // Admin-only: this used to be open to anyone, even before login,
            // which meant any client on the network could create accounts.
            // The very first ("admin") account is still created automatically
            // on server startup (see main()) so this restriction doesn't
            // lock anyone out of initial setup.
            if (!g_auth->isAdmin(currentUser)) { sock.sendLine("ERR|admin only"); continue; }
            if (f.size() < 3) { sock.sendLine("ERR|malformed REGISTER"); continue; }
            if (g_auth->createUser(f[1], f[2])) sock.sendLine("OK|user created");
            else sock.sendLine("ERR|user already exists");
            continue;
        }

        if (cmd == "ADD") {
            if (f.size() < 12) { sock.sendLine("ERR|malformed ADD"); continue; }
            Order o;
            o.customer = f[1]; o.supplier = f[2]; o.description = f[3];
            o.partNumber = f[4]; o.website = f[5]; o.contactName = f[6];
            o.contactNumber = f[7]; o.dateOrdered = f[8].empty() ? todayDateString() : f[8]; o.status = f[9];
            // Quantity defaults to 1 if left blank (or if it's garbage input).
            // Order::calculateTotals() (called inside addOrder()) then derives
            // total = quantity * unitPrice from these two fields - setting
            // o.total directly here would get silently overwritten by that
            // calculation, which is what was zeroing out every saved total
            // before, and is also why a missing quantity was quietly making
            // every subtotal equal to the unit price regardless of how many
            // were actually ordered.
            o.quantity = parseQuantity(f[10]);
            o.unitPrice = std::stod(f[11]);
            // clientName is optional (field 12) so older clients that don't
            // send it yet still work - it just saves blank.
            o.clientName = (f.size() >= 13) ? f[12] : "";
            o.orderedBy = currentUser;
            long id = g_storage->addOrder(o);
            sock.sendLine("OK|" + std::to_string(id));

        } else if (cmd == "FIND") {
            if (f.size() < 2) { sock.sendLine("ERR|malformed FIND"); continue; }
            auto res = g_storage->findById(std::stol(f[1]));
            if (res) { sendRow(sock, *res); sock.sendLine("END"); }
            else sock.sendLine("ERR|not found");

        } else if (cmd == "SEARCH_DESC") {
            if (f.size() < 2) { sock.sendLine("ERR|malformed SEARCH_DESC"); continue; }
            sendRows(sock, g_storage->searchByDescription(f[1]));

        } else if (cmd == "SEARCH_PART") {
            if (f.size() < 2) { sock.sendLine("ERR|malformed SEARCH_PART"); continue; }
            sendRows(sock, g_storage->searchByPartNumber(f[1]));

        } else if (cmd == "SEARCH_SUPPLIER") {
            if (f.size() < 2) { sock.sendLine("ERR|malformed SEARCH_SUPPLIER"); continue; }
            sendRows(sock, g_storage->searchBySupplier(f[1]));

        } else if (cmd == "SEARCH_WHO") {
            if (f.size() < 2) { sock.sendLine("ERR|malformed SEARCH_WHO"); continue; }
            sendRows(sock, g_storage->searchByOrderedBy(f[1]));

        } else if (cmd == "SEARCH_CONTACT_NAME") {
            if (f.size() < 2) { sock.sendLine("ERR|malformed SEARCH_CONTACT_NAME"); continue; }
            sendRows(sock, g_storage->searchByContactName(f[1]));

        } else if (cmd == "SEARCH_CONTACT_NUM") {
            if (f.size() < 2) { sock.sendLine("ERR|malformed SEARCH_CONTACT_NUM"); continue; }
            sendRows(sock, g_storage->searchByContactNumber(f[1]));

        } else if (cmd == "SEARCH_CLIENT") {
            if (f.size() < 2) { sock.sendLine("ERR|malformed SEARCH_CLIENT"); continue; }
            sendRows(sock, g_storage->searchByClientName(f[1]));

        } else if (cmd == "SEARCH_DATE") {
            if (f.size() < 3) { sock.sendLine("ERR|malformed SEARCH_DATE"); continue; }
            // A blank start or end date defaults to today rather than being
            // left unfiltered.
            std::string startDate = f[1].empty() ? todayDateString() : f[1];
            std::string endDate = f[2].empty() ? todayDateString() : f[2];
            sendRows(sock, g_storage->searchByDateRange(startDate, endDate));

        } else if (cmd == "REPORT_MONTH") {
            // Field after the command: "YYYY-MM", e.g. "2026-07".
            if (f.size() < 2 || f[1].size() != 7 || f[1][4] != '-') {
                sock.sendLine("ERR|malformed REPORT_MONTH, expected YYYY-MM");
                continue;
            }
            try {
                int year = std::stoi(f[1].substr(0, 4));
                int month = std::stoi(f[1].substr(5, 2));
                sendRows(sock, g_storage->searchByMonth(year, month));
            } catch (...) {
                sock.sendLine("ERR|malformed REPORT_MONTH, expected YYYY-MM");
            }

        } else if (cmd == "ADVANCED_SEARCH") {
            // Fields after the command name: startDate|endDate|customer|supplier|
            // partNumber|orderedBy|contactNumber|clientName - the non-date fields
            // may be blank to mean "don't filter on this field". The two date
            // fields are different: a blank date defaults to today rather than
            // leaving that side of the range unfiltered (see
            // StorageEngine::advancedSearch).
            if (f.size() < 9) { sock.sendLine("ERR|malformed ADVANCED_SEARCH"); continue; }
            std::string startDate = f[1].empty() ? todayDateString() : f[1];
            std::string endDate = f[2].empty() ? todayDateString() : f[2];
            sendRows(sock, g_storage->advancedSearch(startDate, endDate, f[3], f[4], f[5], f[6], f[7], f[8]));

        } else if (cmd == "UPDATE") {
            if (f.size() < 13) { sock.sendLine("ERR|malformed UPDATE"); continue; }
            Order o;
            o.id = std::stol(f[1]); o.customer = f[2]; o.supplier = f[3];
            o.description = f[4]; o.partNumber = f[5]; o.website = f[6];
            o.contactName = f[7]; o.contactNumber = f[8]; o.dateOrdered = f[9].empty() ? todayDateString() : f[9];
            o.status = f[10];
            o.quantity = parseQuantity(f[11]);
            o.unitPrice = std::stod(f[12]);
            o.orderedBy = currentUser;
            // clientName is optional (field 13) for the same backward-compat
            // reason as in ADD above.
            o.clientName = (f.size() >= 14) ? f[13] : "";
            sock.sendLine(g_storage->updateOrder(o) ? "OK|updated" : "ERR|not found");

        } else if (cmd == "DELETE") {
            if (f.size() < 2) { sock.sendLine("ERR|malformed DELETE"); continue; }
            sock.sendLine(g_storage->deleteOrder(std::stol(f[1])) ? "OK|deleted" : "ERR|not found");

        } else if (cmd == "REPORT") {
            sendRows(sock, g_storage->all());

        } else if (cmd == "CHANGE_PASSWORD") {
            // Self-service: any logged-in user can change their own password,
            // but must supply the correct current one.
            if (f.size() < 3) { sock.sendLine("ERR|malformed CHANGE_PASSWORD"); continue; }
            if (g_auth->changePassword(currentUser, f[1], f[2]))
                sock.sendLine("OK|password changed");
            else
                sock.sendLine("ERR|current password is incorrect");

        } else if (cmd == "ADMIN_RESET_PASSWORD") {
            // Admin-only: reset another user's password without knowing the
            // old one (e.g. they're locked out). No self-service check needed
            // since it also works on your own account.
            if (!g_auth->isAdmin(currentUser)) { sock.sendLine("ERR|admin only"); continue; }
            if (f.size() < 3) { sock.sendLine("ERR|malformed ADMIN_RESET_PASSWORD"); continue; }
            if (g_auth->resetPassword(f[1], f[2]))
                sock.sendLine("OK|password reset for " + f[1]);
            else
                sock.sendLine("ERR|no such user");

        } else if (cmd == "SHUTDOWN") {
            if (!g_auth->isAdmin(currentUser)) { sock.sendLine("ERR|admin only"); continue; }
            std::cout << "[shutdown] Requested by admin '" << currentUser << "'\n";
            sock.sendLine("OK|server shutting down");
            // Give the response a moment to actually reach the client before
            // the process exits. Storage writes are flushed after every
            // append (see StorageEngine::appendLine), so there's no unsaved
            // data at risk from an immediate exit here.
            std::thread([]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(300));
                std::exit(0);
            }).detach();
            break;

        } else {
            sock.sendLine("ERR|unknown command");
        }
    }

    int remaining = g_activeConnections.fetch_sub(1) - 1;
    logConnectionEvent("Disconnected: " + peerAddr +
                        (currentUser.empty() ? "" : " (was logged in as '" + currentUser + "')") +
                        " (" + std::to_string(remaining) + "/" + std::to_string(MAX_CONNECTIONS) + " active)");
}

int main(int argc, char** argv) {
    int port = 5050;
    if (argc > 1) port = std::stoi(argv[1]);

    if (!socketLibInit()) {
        std::cerr << "Failed to initialize sockets\n";
        return 1;
    }

    StorageEngine storage("data/orders.dat");
    AuthManager auth("data/users.dat");
    if (!storage.load()) { std::cerr << "Failed to load order data\n"; return 1; }
    auth.load();
    g_storage = &storage;
    g_auth = &auth;

    // Bootstrap: if no users exist yet, create a default admin so you're not
    // locked out on first run. Change this password immediately (menu option
    // for this is on the client - self-service password change).
    if (!auth.userExists("admin")) {
        auth.createUser("admin", "changeme", /*isAdmin=*/true);
        std::cout << "Created default account admin/changeme (admin role) - change this password.\n";
    }

    ServerSocket server;
    if (!server.listenOn(port)) {
        std::cerr << "Failed to listen on port " << port << "\n";
        return 1;
    }
    std::cout << "OrderDB server listening on port " << port
              << " (max " << MAX_CONNECTIONS << " concurrent connections)\n";

    while (true) {
        Socket client = server.accept();
        if (!client.valid()) continue;

        // main() is the only thread that ever increments this counter, and
        // it does so sequentially in this loop, so the check-then-increment
        // here can't race with itself - only decrements (from handleClient
        // threads finishing) happen concurrently, and those are independent
        // atomic operations.
        int current = g_activeConnections.load();
        if (current >= MAX_CONNECTIONS) {
            logConnectionEvent("REJECTED " + client.peerAddress() +
                                " - server full (" + std::to_string(current) +
                                "/" + std::to_string(MAX_CONNECTIONS) + ")");
            client.sendLine("ERR|server is full (max " + std::to_string(MAX_CONNECTIONS) +
                             " connections), try again later");
            client.close();
            continue;
        }

        int nowActive = g_activeConnections.fetch_add(1) + 1;
        logConnectionEvent("Connected: " + client.peerAddress() +
                            " (" + std::to_string(nowActive) + "/" +
                            std::to_string(MAX_CONNECTIONS) + " active)");

        std::thread(handleClient, std::move(client)).detach();
    }

    socketLibCleanup();
    return 0;
}
