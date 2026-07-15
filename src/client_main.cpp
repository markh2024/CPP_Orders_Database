#include "Socket.h"
#include "Protocol.h"
#include "Colors.h"

#include <iostream>
#include <string>
#include <limits>
#include <cstdlib>
#include <iomanip>
#include <algorithm>

// ----------------------------------------------------
// Display returned database rows
// ----------------------------------------------------

static void printRows(Socket& sock)
{
    std::string line;
    int count = 0;
    double sumOfTotals = 0.0;

    while (sock.recvLine(line))
    {
        if (line == "END")
            break;

        auto f = splitFields(line);

        if (f.size() >= 15 && f[0] == "ROW")
        {
            std::cout
                << "\n" << Color::AMBER << "ID:          " << Color::RESET << f[1]
                << "\n" << Color::AMBER << "Customer:    " << Color::RESET << f[2]
                << "\n" << Color::AMBER << "Supplier:    " << Color::RESET << f[3]
                << "\n" << Color::AMBER << "Description: " << Color::RESET << f[4]
                << "\n" << Color::AMBER << "Part No:     " << Color::RESET << f[5]
                << "\n" << Color::AMBER << "Website:     " << Color::RESET << f[6]
                << "\n" << Color::AMBER << "Contact:     " << Color::RESET << f[7]
                << "\n" << Color::AMBER << "Phone:       " << Color::RESET << f[8]
                << "\n" << Color::AMBER << "Ordered By:  " << Color::RESET << f[9]
                << "\n" << Color::AMBER << "Date:        " << Color::RESET << f[10]
                << "\n" << Color::AMBER << "Status:      " << Color::RESET << f[11]
                << "\n" << Color::AMBER << "Quantity:    " << Color::RESET << f[14]
                << "\n" << Color::AMBER << "Total:       " << Color::BOLD_GREEN << f[12] << Color::RESET
                << "\n" << Color::PINK  << "Client Name: " << Color::RESET << f[13]
                << "\n" << Color::GRAY  << "-----------------------------" << Color::RESET << "\n";

            count++;
            try { sumOfTotals += std::stod(f[12]); } catch (...) {}
        }
        else if (!f.empty() && (f[0] == "ERR" || f[0] == "OK"))
        {
            // The server replied with a single status line instead of a row
            // stream (e.g. an unrecognized command, or a malformed request).
            // Stop here instead of waiting forever for an END that isn't
            // coming - that wait-forever is what caused the Advanced Search
            // hang.
            std::cout << "\n";
            printResponse(line);
            return;
        }
        // Anything else unrecognized is ignored, same as before, but ERR/OK
        // above guarantees we can't loop forever on a single-line response.
    }

    std::cout
        << "\n" << Color::CYAN << "(" << count << " result"
        << (count == 1 ? "" : "s")
        << ")" << Color::RESET << "\n";

    // Running sum applies uniformly to every report/search here - client
    // search, supplier search, advanced search, date range, monthly report,
    // and the full report all funnel through this one function.
    if (count > 0)
    {
        std::cout
            << Color::BOLD_GREEN << "Sum of all Totals shown above: "
            << std::fixed << std::setprecision(2) << sumOfTotals
            << Color::RESET << "\n";
    }
}



// ----------------------------------------------------
// Pause before returning to menu
// ----------------------------------------------------

static void pauseScreen()
{
    std::cout << "\n" << Color::GRAY << "Press ENTER to continue..." << Color::RESET;

    std::cin.ignore(
        std::numeric_limits<std::streamsize>::max(),
        '\n'
    );

    std::cin.get();
}

// ----------------------------------------------------
// Cancellable field entry for multi-step forms (Add/Update Order)
// ----------------------------------------------------
//
// Typing 'cancel' (any case, surrounding whitespace ignored) at any prompt
// during order entry aborts the whole form. Since the ADD/UPDATE command is
// only ever sent once, after every field has been collected, cancelling
// partway through simply means the caller never sends that command at all -
// nothing partial ever reaches the server, so there's nothing to roll back
// there.

static std::string trimmed(const std::string& s)
{
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

static bool isCancelWord(const std::string& s)
{
    std::string lower = trimmed(s);
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    return lower == "cancel";
}

// Prompts for a single field. Returns true if the user typed 'cancel'
// instead (in which case 'out' should not be trusted/used).
static bool readFieldOrCancel(const std::string& prompt, std::string& out)
{
    printLabel(prompt);
    std::getline(std::cin, out);
    return isCancelWord(out);
}

// Shows a fixed numbered menu of order statuses so status can't drift into
// inconsistent free text (e.g. "ORDERD" vs "ordered" being treated as
// different statuses by search/reporting). Loops until a valid 1-6 choice
// or 'cancel'. Sets cancelled=true and returns "" if the user cancels.
static std::string promptStatusOrCancel(bool& cancelled)
{
    static const std::string statuses[] = {
        "Pending", "Ordered", "Back ordered",
        "Shipped", "Received", "Cancelled"
    };

    while (true)
    {
        std::cout << "\n" << Color::BOLD_CYAN << "Status:" << Color::RESET << "\n";
        for (int i = 0; i < 6; ++i)
            std::cout << "  " << Color::AMBER << (i + 1) << Color::RESET
                       << ": " << statuses[i] << "\n";
        printLabel("Select 1-6 (or 'cancel' to abort this order): ");

        std::string input;
        std::getline(std::cin, input);

        if (isCancelWord(input))
        {
            cancelled = true;
            return "";
        }

        try
        {
            int choice = std::stoi(trimmed(input));
            if (choice >= 1 && choice <= 6)
                return statuses[choice - 1];
        }
        catch (...)
        {
            // falls through to the "invalid" message below
        }

        std::cout << Color::BOLD_RED << "Invalid selection - please enter a number from 1 to 6."
                   << Color::RESET << "\n";
    }
}

// ----------------------------------
// clear screen 
//  ---------------------------------

static void clearScreen()
{
#ifdef _WIN32
    system("cls");
#else
    system("clear");
#endif
}



// ----------------------------------------------------
// Main menu
// ----------------------------------------------------

static void printMenuItem(const std::string& number, const std::string& text, const char* color)
{
    std::cout << "  " << color << number << Color::RESET << ". " << text << "\n";
}

static void showMenu()
{
    std::cout << "\n";
    std::cout << Color::BOLD_CYAN << "=========================================\n";
    std::cout << "          ORDER DATABASE CLIENT\n";
    std::cout << "=========================================" << Color::RESET << "\n";

    printMenuItem("1",  "Add Order",                  Color::GREEN);
    printMenuItem("2",  "Find Order by ID",            Color::GREEN);
    std::cout << "\n";
    printMenuItem("3",  "Search Description",          Color::CYAN);
    printMenuItem("4",  "Search Part Number",          Color::CYAN);
    printMenuItem("5",  "Search Supplier",             Color::CYAN);
    printMenuItem("6",  "Search Ordered By",           Color::CYAN);
    printMenuItem("7",  "Search Contact Name",         Color::CYAN);
    printMenuItem("8",  "Search Contact Number",       Color::CYAN);
    printMenuItem("9",  "Search Date Range",           Color::CYAN);
    printMenuItem("14", "Search Client Name",          Color::CYAN);
    std::cout << "\n";
    printMenuItem("10", "Update Order",                Color::GREEN);
    printMenuItem("11", "Delete Order",                Color::GREEN);
    std::cout << "\n";
    printMenuItem("12", "Report (List All Orders)",    Color::AMBER);
    printMenuItem("13", "Advanced Search Report",      Color::AMBER);
    printMenuItem("15", "Monthly Report",              Color::AMBER);
    std::cout << "\n";
    printMenuItem("16", "Change My Password",          Color::PINK);
    printMenuItem("17", "Admin: Reset User Password",  Color::BOLD_RED);
    printMenuItem("18", "Admin: Register New User",    Color::BOLD_RED);
    printMenuItem("19", "Admin: Shutdown Server",      Color::BOLD_RED);
    std::cout << "\n";
    printMenuItem("20", "Quit",                        Color::GRAY);

    std::cout << "\n";
    printLabel("Selection : ");
}


// ----------------------------------------------------
// Program entry
// ----------------------------------------------------

int main(int argc, char** argv)
{
    enableAnsiOnWindows(); // no-op on Linux, needed for colors on Windows 10+ consoles

    std::string host = "127.0.0.1";
    int port = 5050;


    if (argc > 1)
        host = argv[1];

    if (argc > 2)
        port = std::stoi(argv[2]);



    if (!socketLibInit())
    {
        std::cerr
            << "Socket initialisation failed\n";

        return 1;
    }



    auto sock = connectTo(host, port);


    if (!sock)
    {
        std::cerr
            << Color::BOLD_RED << "Could not connect to "
            << host << ":" << port
            << Color::RESET << "\n";

        socketLibCleanup();
        return 1;
    }



    std::cout
        << Color::BOLD_GREEN << "Connected to OrderDB server at "
        << host << ":" << port
        << Color::RESET << "\n";



    // ------------------------------------------------
    // Login
    // ------------------------------------------------

    std::string user;
    std::string pass;
    std::string resp;


    printSectionTitle("LOGIN");
    printLabel("Username: ");
    std::getline(std::cin, user);


    printLabel("Password: ");
    std::getline(std::cin, pass);



    sock->sendLine(
        joinFields({"LOGIN", user, pass})
    );


    if (!sock->recvLine(resp))
    {
        std::cerr
            << Color::BOLD_RED << "Server disconnected" << Color::RESET << "\n";

        return 1;
    }


    printResponse(resp);


    if (resp.rfind("OK", 0) != 0)
    {
        socketLibCleanup();
        return 1;
    }



    // ------------------------------------------------
    // Menu loop starts in Part 2
    // ------------------------------------------------
    
    
        // ------------------------------------------------
    // Menu loop
    // ------------------------------------------------

    while (true)
    {
		
		clearScreen();
		
        showMenu();


        int choice;


        if (!(std::cin >> choice))
        {
            if (std::cin.eof())
            {
                // Input stream closed (e.g. stdin redirected from a file/pipe
                // that ran out, or the terminal session dropped). Without this
                // check, cin.clear() below would keep resetting the EOF flag
                // every iteration and this loop would spin forever reading
                // nothing, pegging the CPU instead of exiting.
                std::cout << "\nInput closed - exiting.\n";
                sock->sendLine("QUIT");
                socketLibCleanup();
                return 0;
            }

            std::cin.clear();

            std::cin.ignore(
                std::numeric_limits<std::streamsize>::max(),
                '\n'
            );

            std::cout
                << "Invalid selection\n";

            continue;
        }


        std::cin.ignore(
            std::numeric_limits<std::streamsize>::max(),
            '\n'
        );



        // ------------------------------------------------
        // 1. Add Order
        // ------------------------------------------------

        if (choice == 1)
        {
            std::string customer;
            std::string supplier;
            std::string desc;
            std::string partNum;
            std::string website;
            std::string contactName;
            std::string contactNum;
            std::string date;
            std::string status;
            std::string qty;
            std::string unitPrice;
            std::string clientName;
            bool cancelled = false;

            printSectionTitle("ADD ORDER");
            std::cout << Color::GRAY << "(Type 'cancel' at any prompt to abort this order - nothing will be saved.)"
                       << Color::RESET << "\n\n";

            cancelled = cancelled || readFieldOrCancel("Customer: ", customer);
            cancelled = cancelled || readFieldOrCancel("Supplier: ", supplier);
            cancelled = cancelled || readFieldOrCancel("Description: ", desc);
            cancelled = cancelled || readFieldOrCancel("Part number: ", partNum);
            cancelled = cancelled || readFieldOrCancel("Website: ", website);
            cancelled = cancelled || readFieldOrCancel("Contact name: ", contactName);
            cancelled = cancelled || readFieldOrCancel("Contact number: ", contactNum);
            cancelled = cancelled || readFieldOrCancel("Date ordered (YYYY-MM-DD, blank = today): ", date);

            if (!cancelled)
                status = promptStatusOrCancel(cancelled);

            cancelled = cancelled || readFieldOrCancel("Quantity (press Enter for 1): ", qty);
            if (!cancelled && qty.empty()) qty = "1";

            cancelled = cancelled || readFieldOrCancel("Unit Price: ", unitPrice);
            cancelled = cancelled || readFieldOrCancel(
                "Client name (person who requested this order): ", clientName);

            if (cancelled)
            {
                std::cout << "\nOrder entry cancelled - nothing was saved.\n";
                pauseScreen();
            }
            else
            {
                sock->sendLine(
                    joinFields({
                        "ADD",
                        customer,
                        supplier,
                        desc,
                        partNum,
                        website,
                        contactName,
                        contactNum,
                        date,
                        status,
                        qty,
                        unitPrice,
                        clientName
                    })
                );


                sock->recvLine(resp);

                printResponse(resp);

                pauseScreen();
            }
        }



        // ------------------------------------------------
        // 2. Find Order by ID
        // ------------------------------------------------

        else if (choice == 2)
        {
            std::string id;

            printSectionTitle("FIND ORDER BY ID");
            printLabel("Order ID: ");
            std::getline(std::cin, id);


            sock->sendLine(
                joinFields({"FIND", id})
            );


            printRows(*sock);


            pauseScreen();
        }



        // ------------------------------------------------
        // 3. Search Description
        // ------------------------------------------------

        else if (choice == 3)
        {
            std::string keyword;

            printSectionTitle("SEARCH DESCRIPTION");
            printLabel("Description keyword: ");
            std::getline(std::cin, keyword);


            sock->sendLine(
                joinFields({
                    "SEARCH_DESC",
                    keyword
                })
            );


            printRows(*sock);


            pauseScreen();
        }



        // ------------------------------------------------
        // 4. Search Part Number
        // ------------------------------------------------

        else if (choice == 4)
        {
            std::string part;

            printSectionTitle("SEARCH PART NUMBER");
            printLabel("Part number: ");
            std::getline(std::cin, part);


            sock->sendLine(
                joinFields({
                    "SEARCH_PART",
                    part
                })
            );


            printRows(*sock);


            pauseScreen();
        }



        // ------------------------------------------------
        // 5. Search Supplier
        // ------------------------------------------------

        else if (choice == 5)
        {
            std::string supplier;

            printSectionTitle("SEARCH SUPPLIER");
            printLabel("Supplier: ");
            std::getline(std::cin, supplier);


            sock->sendLine(
                joinFields({
                    "SEARCH_SUPPLIER",
                    supplier
                })
            );


            printRows(*sock);


            pauseScreen();
        }



        // Options 6-13 continue in Part 3 and Part 4
        // ------------------------------------------------
        // 6. Search Ordered By
        // ------------------------------------------------

        else if (choice == 6)
        {
            std::string orderedBy;

            printSectionTitle("SEARCH ORDERED BY");
            printLabel("Ordered by username: ");
            std::getline(std::cin, orderedBy);


            sock->sendLine(
                joinFields({
                    "SEARCH_WHO",
                    orderedBy
                })
            );


            printRows(*sock);


            pauseScreen();
        }



        // ------------------------------------------------
        // 7. Search Contact Name
        // ------------------------------------------------

        else if (choice == 7)
        {
            std::string contact;

            printSectionTitle("SEARCH CONTACT NAME");
            printLabel("Contact name: ");
            std::getline(std::cin, contact);


            sock->sendLine(
                joinFields({
                    "SEARCH_CONTACT_NAME",
                    contact
                })
            );


            printRows(*sock);


            pauseScreen();
        }



        // ------------------------------------------------
        // 8. Search Contact Number
        // ------------------------------------------------

        else if (choice == 8)
        {
            std::string contactNum;

            printSectionTitle("SEARCH CONTACT NUMBER");
            printLabel("Contact number: ");
            std::getline(std::cin, contactNum);


            sock->sendLine(
                joinFields({
                    "SEARCH_CONTACT_NUM",
                    contactNum
                })
            );


            printRows(*sock);


            pauseScreen();
        }



        // ------------------------------------------------
        // 9. Search Date Range
        // ------------------------------------------------

        else if (choice == 9)
        {
            std::string startDate;
            std::string endDate;

            printSectionTitle("SEARCH DATE RANGE");
            printLabel("Start date (YYYY-MM-DD, blank = today): ");
            std::getline(std::cin, startDate);

            printLabel("End date (YYYY-MM-DD, blank = today): ");
            std::getline(std::cin, endDate);



            sock->sendLine(
                joinFields({
                    "SEARCH_DATE",
                    startDate,
                    endDate
                })
            );


            printRows(*sock);


            pauseScreen();
        }



        // ------------------------------------------------
        // 10. Update Order
        // ------------------------------------------------

        else if (choice == 10)
        {
            std::string id;
            std::string customer;
            std::string supplier;
            std::string desc;
            std::string partNum;
            std::string website;
            std::string contactName;
            std::string contactNum;
            std::string date;
            std::string status;
            std::string qty;
            std::string unitPrice;
            std::string clientName;
            bool cancelled = false;

            printSectionTitle("UPDATE ORDER");
            std::cout << Color::GRAY << "(Type 'cancel' at any prompt to abort this update - nothing will be saved.)"
                       << Color::RESET << "\n\n";

            cancelled = cancelled || readFieldOrCancel("Order ID: ", id);
            cancelled = cancelled || readFieldOrCancel("Customer: ", customer);
            cancelled = cancelled || readFieldOrCancel("Supplier: ", supplier);
            cancelled = cancelled || readFieldOrCancel("Description: ", desc);
            cancelled = cancelled || readFieldOrCancel("Part number: ", partNum);
            cancelled = cancelled || readFieldOrCancel("Website: ", website);
            cancelled = cancelled || readFieldOrCancel("Contact name: ", contactName);
            cancelled = cancelled || readFieldOrCancel("Contact number: ", contactNum);
            cancelled = cancelled || readFieldOrCancel("Date ordered (YYYY-MM-DD, blank = today): ", date);

            if (!cancelled)
                status = promptStatusOrCancel(cancelled);

            cancelled = cancelled || readFieldOrCancel("Quantity (press Enter for 1): ", qty);
            if (!cancelled && qty.empty()) qty = "1";

            cancelled = cancelled || readFieldOrCancel("Unit Price: ", unitPrice);
            cancelled = cancelled || readFieldOrCancel(
                "Client name (person who requested this order): ", clientName);

            if (cancelled)
            {
                std::cout << "\nUpdate cancelled - nothing was saved.\n";
                pauseScreen();
            }
            else
            {
                sock->sendLine(
                    joinFields({
                        "UPDATE",
                        id,
                        customer,
                        supplier,
                        desc,
                        partNum,
                        website,
                        contactName,
                        contactNum,
                        date,
                        status,
                        qty,
                        unitPrice,
                        clientName
                    })
                );


                sock->recvLine(resp);


                printResponse(resp);

                pauseScreen();
            }
        }



        // Options 11-13 continue in Part 4
                // ------------------------------------------------
        // 11. Delete Order
        // ------------------------------------------------

        else if (choice == 11)
        {
            std::string id;

            printSectionTitle("DELETE ORDER");
            printLabel("Order ID to delete: ");
            std::getline(std::cin, id);


            sock->sendLine(
                joinFields({
                    "DELETE",
                    id
                })
            );


            sock->recvLine(resp);
            printResponse(resp);


            pauseScreen();
        }



        // ------------------------------------------------
        // 12. Report - List All Orders
        // ------------------------------------------------

        else if (choice == 12)
        {
            printSectionTitle("REPORT - ALL ORDERS");
            sock->sendLine("REPORT");


            printRows(*sock);


            pauseScreen();
        }



		// ------------------------------------------------
// 13. Advanced Search Report
// ------------------------------------------------

else if (choice == 13)
{
    std::string startDate;
    std::string endDate;
    std::string customer;
    std::string supplier;
    std::string partNumber;
    std::string orderedBy;
    std::string contactNumber;
    std::string clientName;

    printSectionTitle("ADVANCED SEARCH REPORT");

    printLabel("Start date (YYYY-MM-DD, blank = today): ");
    std::getline(std::cin, startDate);


    printLabel("End date (YYYY-MM-DD, blank = today): ");
    std::getline(std::cin, endDate);


    printLabel("Customer name (blank for all): ");
    std::getline(std::cin, customer);


    printLabel("Supplier (blank for all): ");
    std::getline(std::cin, supplier);


    printLabel("Part number (blank for all): ");
    std::getline(std::cin, partNumber);


    printLabel("Ordered by (blank for all): ");
    std::getline(std::cin, orderedBy);


    printLabel("Contact number (blank for all): ");
    std::getline(std::cin, contactNumber);


    printLabel("Client name (blank for all): ");
    std::getline(std::cin, clientName);



    sock->sendLine(
        joinFields({
            "ADVANCED_SEARCH",
            startDate,
            endDate,
            customer,
            supplier,
            partNumber,
            orderedBy,
            contactNumber,
            clientName
        })
    );


    printRows(*sock);


    pauseScreen();
}



// ------------------------------------------------
// 14. Search Client Name
// ------------------------------------------------

else if (choice == 14)
{
    std::string clientName;

    printSectionTitle("SEARCH CLIENT NAME");
    printLabel("Client name (or part of it): ");
    std::getline(std::cin, clientName);

    sock->sendLine(joinFields({"SEARCH_CLIENT", clientName}));

    printRows(*sock);

    pauseScreen();
}



// ------------------------------------------------
// 15. Monthly Report
// ------------------------------------------------

else if (choice == 15)
{
    std::string yearMonth;

    printSectionTitle("MONTHLY REPORT");
    printLabel("Month to report on (YYYY-MM, e.g. 2026-07): ");
    std::getline(std::cin, yearMonth);

    sock->sendLine(joinFields({"REPORT_MONTH", yearMonth}));

    printRows(*sock);

    pauseScreen();
}



// ------------------------------------------------
// 16. Change My Password
// ------------------------------------------------

else if (choice == 16)
{
    std::string oldPass, newPass, confirmPass;

    printSectionTitle("CHANGE MY PASSWORD");
    printLabel("Current password: ");
    std::getline(std::cin, oldPass);

    printLabel("New password: ");
    std::getline(std::cin, newPass);

    printLabel("Confirm new password: ");
    std::getline(std::cin, confirmPass);

    if (newPass != confirmPass)
    {
        std::cout << Color::BOLD_RED << "New password entries don't match - not changed."
                   << Color::RESET << "\n";
    }
    else
    {
        sock->sendLine(joinFields({"CHANGE_PASSWORD", oldPass, newPass}));
        sock->recvLine(resp);
        printResponse(resp);
    }

    pauseScreen();
}



// ------------------------------------------------
// 17. Admin: Reset User Password
// ------------------------------------------------

else if (choice == 17)
{
    std::string username, newPass;

    printSectionTitle("ADMIN: RESET USER PASSWORD");
    printLabel("Username to reset: ");
    std::getline(std::cin, username);

    printLabel("New password for that user: ");
    std::getline(std::cin, newPass);

    sock->sendLine(joinFields({"ADMIN_RESET_PASSWORD", username, newPass}));
    sock->recvLine(resp);
    printResponse(resp);

    pauseScreen();
}



// ------------------------------------------------
// 18. Admin: Register New User
// ------------------------------------------------

else if (choice == 18)
{
    std::string username, newPass;

    printSectionTitle("ADMIN: REGISTER NEW USER");
    std::cout << Color::GRAY
               << "Creates a login for a real person, so orders they add show up\n"
               << "under their own username instead of everyone using 'admin'."
               << Color::RESET << "\n";
    printLabel("New username: ");
    std::getline(std::cin, username);

    printLabel("Password for that user: ");
    std::getline(std::cin, newPass);

    sock->sendLine(joinFields({"REGISTER", username, newPass}));
    sock->recvLine(resp);
    printResponse(resp);

    pauseScreen();
}



// ------------------------------------------------
// 19. Admin: Shutdown Server
// ------------------------------------------------

else if (choice == 19)
{
    std::string confirm;

    printSectionTitle("ADMIN: SHUTDOWN SERVER");
    std::cout << Color::BOLD_RED
               << "This will shut down the server for ALL connected clients."
               << Color::RESET << "\n";
    printLabel("Type YES to confirm: ");
    std::getline(std::cin, confirm);

    if (confirm == "YES")
    {
        sock->sendLine("SHUTDOWN");
        if (sock->recvLine(resp))
            printResponse(resp);

        std::cout << Color::BOLD_RED << "Server is shutting down. Exiting client."
                   << Color::RESET << "\n";
        socketLibCleanup();
        return 0;
    }
    else
    {
        std::cout << Color::GRAY << "Shutdown cancelled." << Color::RESET << "\n";
        pauseScreen();
    }
}



        // ------------------------------------------------
        // 20. Quit
        // ------------------------------------------------

        else if (choice == 20)
        {
            sock->sendLine("QUIT");


            if (sock->recvLine(resp))
            {
                printResponse(resp);
            }


            break;
        }



        // ------------------------------------------------
        // Invalid menu option
        // ------------------------------------------------

        else
        {
            std::cout
                << Color::BOLD_RED << "Invalid menu selection" << Color::RESET << "\n";

            pauseScreen();
        }

    } // end while menu loop



    socketLibCleanup();


    return 0;

}
        
