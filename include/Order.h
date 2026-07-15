#pragma once

#include <string>
#include <sstream>
#include <vector>


// Delimiter used inside a single record line
constexpr char FIELD_DELIM = '|';


struct Order
{
    long id = -1;

    std::string customer;
    std::string supplier;
    std::string description;
    std::string partNumber;
    std::string website;

    std::string contactName;
    std::string contactNumber;

    std::string orderedBy;
    std::string dateOrdered;
    std::string status;

    // Contact person on the client/requester side (as opposed to
    // contactName above, which is the contact at the supplier).
    std::string clientName;


    // ------------------------------------------------
    // Order quantity and pricing
    // ------------------------------------------------

    int quantity = 1;

    double unitPrice = 0.0;

    double subtotal = 0.0;


    // Reserved for future:
    // subtotal + VAT + delivery - discount
    double total = 0.0;



    // ------------------------------------------------
    // Calculate totals
    // ------------------------------------------------

    void calculateTotals()
    {
        subtotal = quantity * unitPrice;

        // currently total equals subtotal
        total = subtotal;
    }



    // ------------------------------------------------
    // Serialize
    // ------------------------------------------------

    std::string serialize() const
    {
        auto esc = [](const std::string& s)
        {
            std::string out;

            out.reserve(s.size());

            for(char c : s)
            {
                if(c == FIELD_DELIM ||
                   c == '\\' ||
                   c == '\n')
                {
                    out += '\\';
                }


                out += (c == '\n') ? 'n' : c;
            }

            return out;
        };



        std::ostringstream oss;


        oss
            << id << FIELD_DELIM

            << esc(customer) << FIELD_DELIM
            << esc(supplier) << FIELD_DELIM
            << esc(description) << FIELD_DELIM
            << esc(partNumber) << FIELD_DELIM
            << esc(website) << FIELD_DELIM

            << esc(contactName) << FIELD_DELIM
            << esc(contactNumber) << FIELD_DELIM

            << esc(orderedBy) << FIELD_DELIM
            << esc(dateOrdered) << FIELD_DELIM
            << esc(status) << FIELD_DELIM


            // New fields

            << quantity << FIELD_DELIM
            << unitPrice << FIELD_DELIM
            << subtotal << FIELD_DELIM
            << total << FIELD_DELIM

            << esc(clientName);



        return oss.str();
    }




    // ------------------------------------------------
    // Deserialize
    //
    // Supports three generations of stored data, so old rows still load:
    //
    // OLDEST:  id.....status|total
    // MIDDLE:  id.....status|qty|price|subtotal|total
    // CURRENT: id.....status|qty|price|subtotal|total|clientName
    //
    // ------------------------------------------------

    static Order deserialize(const std::string& line)
    {
        std::vector<std::string> fields;

        std::string cur;


        for(size_t i = 0; i < line.size(); i++)
        {
            char c = line[i];


            if(c == '\\' && i + 1 < line.size())
            {
                char n = line[++i];

                cur += (n == 'n') ? '\n' : n;
            }


            else if(c == FIELD_DELIM)
            {
                fields.push_back(cur);
                cur.clear();
            }


            else
            {
                cur += c;
            }
        }


        fields.push_back(cur);



        Order o;



        if(fields.size() >= 12)
        {

            o.id = std::stol(fields[0]);

            o.customer = fields[1];
            o.supplier = fields[2];

            o.description = fields[3];
            o.partNumber = fields[4];

            o.website = fields[5];

            o.contactName = fields[6];
            o.contactNumber = fields[7];

            o.orderedBy = fields[8];

            o.dateOrdered = fields[9];

            o.status = fields[10];



            // ----------------------------------------
            // New database format
            // ----------------------------------------

            if(fields.size() >= 15)
            {
                o.quantity =
                    std::stoi(fields[11]);


                o.unitPrice =
                    std::stod(fields[12]);


                o.subtotal =
                    std::stod(fields[13]);


                o.total =
                    std::stod(fields[14]);

                // clientName was added after the pricing fields, so it's
                // only present in the newest rows (field 15). Older rows
                // that already have pricing but predate clientName just
                // get an empty string here, which is fine.
                o.clientName = (fields.size() >= 16) ? fields[15] : "";
            }


            // ----------------------------------------
            // Old database format
            // ----------------------------------------

            else
            {
                o.quantity = 1;


                o.unitPrice =
                    std::stod(fields[11]);


                o.subtotal =
                    o.unitPrice;


                o.total =
                    o.unitPrice;

                o.clientName = "";
            }
        }


        return o;
    }

};
