#pragma once
#include <string>
#include <vector>
#include <sstream>

// Wire protocol: one command per line, fields separated by '|'.
// Client -> Server examples:
//   LOGIN|alice|secret123
//   REGISTER|bob|somepassword                     (admin role only)
//     Creates a new login. The very first ("admin") account is created
//     automatically on server startup instead, so this restriction doesn't
//     block initial setup.
//   ADD|Acme Corp|Bolt Suppliers Ltd|M8 bolts, box of 100|BS-M8-100|https://boltsuppliers.example|
//       Jane Smith|555-1234|2026-07-01|pending|5|9.00|Bob (requester)
//     (fields after ADD: customer|supplier|description|partNumber|website|contactName|
//      contactNumber|dateOrdered|status|quantity|unitPrice|clientName)
//     dateOrdered: blank defaults to today's date, not an empty/invalid record.
//     quantity is OPTIONAL - leave it blank for a default of 1. total = quantity * unitPrice
//     is computed server-side (Order::calculateTotals()), not sent by the client.
//     clientName (last field) is also OPTIONAL - omit it entirely for backward compatibility
//     with older clients; it just saves blank.
//     orderedBy is NOT a client-supplied field - the server always sets it from
//     whoever is logged in for this connection, so it can't be spoofed. If you
//     want orders to show a specific person as having placed them, that person
//     needs their own login (see REGISTER above) rather than typing a name in.
//   SEARCH_DESC|bolt
//   SEARCH_PART|BS-M8
//   SEARCH_SUPPLIER|Bolt Suppliers Ltd
//   SEARCH_WHO|alice
//   SEARCH_CONTACT_NAME|Jane
//   SEARCH_CONTACT_NUM|555-1234
//   SEARCH_CLIENT|Bob
//   SEARCH_DATE|2026-01-01|2026-12-31
//     (either date may be blank, which defaults that side of the range to today)
//   REPORT_MONTH|2026-07                          (lists every order in July 2026)
//   ADVANCED_SEARCH|startDate|endDate|customer|supplier|partNumber|orderedBy|contactNumber|clientName
//     (startDate/endDate: blank defaults to today, not "unfiltered".
//      all other fields: blank means "don't filter on this field")
//   FIND|17
//   UPDATE|17|Acme Corp|Bolt Suppliers Ltd|M8 bolts, box of 100|BS-M8-100|
//       https://boltsuppliers.example|Jane Smith|555-1234|2026-07-01|shipped|5|9.00|Bob (requester)
//     (dateOrdered blank defaults to today, same as ADD. quantity, clientName - both
//      optional, same rules as ADD above)
//   DELETE|17
//   REPORT
//   CHANGE_PASSWORD|oldPassword|newPassword
//   ADMIN_RESET_PASSWORD|username|newPassword   (admin role only)
//   SHUTDOWN                                    (admin role only)
//   QUIT
//
// Server -> Client:
//   OK|<message or blank>
//   ERR|<message>
//   ROW|<id>|<customer>|<supplier>|<description>|<partNumber>|<website>|<contactName>|
//       <contactNumber>|<orderedBy>|<date>|<status>|<total>|<clientName>|<quantity>
//   END      (marks end of a multi-row response)

inline std::vector<std::string> splitFields(const std::string& line) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : line) {
        if (c == '|') { out.push_back(cur); cur.clear(); }
        else cur += c;
    }
    out.push_back(cur);
    return out;
}

inline std::string joinFields(const std::vector<std::string>& fields) {
    std::ostringstream oss;
    for (size_t i = 0; i < fields.size(); ++i) {
        if (i) oss << "|";
        oss << fields[i];
    }
    return oss.str();
}
