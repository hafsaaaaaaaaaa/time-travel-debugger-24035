#include <iostream>
#include <string>
#include "validate.h"
#include "resolve.h"
#include "execute.h"
#include "serialize.h"

int main(int argc, char *argv[])
{
    if (argc > 2)
    {
        std::cout << "usage: ./ttdb [source file]" << std::endl;
        return 1;
    }

    std::string sourcePath = "source.bin";

    if (argc == 2)
    {
        sourcePath = argv[1];
    }

    std::string error;

    if (!validate(sourcePath.c_str(), error))
    {
        std::cout << "error: " << error << std::endl;
        return 1;
    }

    int64_t mainOffset = resolve(sourcePath.c_str(), "resolve.bin", error);

    if (mainOffset == -1)
    {
        std::cout << "error: " << error << std::endl;
        return 1;
    }

    Timeline timeline;

    if (!execute("resolve.bin", mainOffset, timeline, error))
    {
        std::cout << "error: " << error << std::endl;
        return 1;
    }

    writeTdbg(timeline, "session.tdbg");
    return 0;
}
