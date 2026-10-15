#include <fstream>
#include <sstream>
#include <stack>
#include "validate.h"

bool validate(const string &path, string &error)
{
    ifstream in(path);
    if (!in)
    {
        error = "cannot open " + path;
        return false;
    }

    stack<int> open;
    string line;
    int lineNo = 0;

    while (getline(in, line))
    {
        lineNo++;
        istringstream iss(line);
        string keyword;
        if (!(iss >> keyword))
        {
            continue;
        }

        if (keyword == "func")
        {
            string name;
            if (!(iss >> name))
            {
                error = "line " + to_string(lineNo) + ": func needs a name";
                return false;
            }

            int count = 0;
            string param;
            while (iss >> param)
            {
                count++;
            }

            if (count > 16)
            {
                error = "line " + to_string(lineNo) + ": too many parameters";
                return false;
            }

            if (!open.empty())
            {
                error = "line " + to_string(lineNo) + ": nested function, function at line " + to_string(open.top()) + " is not closed";
                return false;
            }
            open.push(lineNo);
        }
        else if (keyword == "func_end")
        {
            if (open.empty())
            {
                error = "line " + to_string(lineNo) + ": func_end without a matching func";
                return false;
            }
            open.pop();
        }
    }

    if (!open.empty())
    {
        error = "line " + to_string(open.top()) + ": function without a matching func_end";
        return false;
    }

    return true;
}
