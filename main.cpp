#include <iostream>
#include "validate.h"
#include "resolve.h"
#include "execute.h"
#include "serialize.h"

int main(int argc,char *argv[])
{   if (argc<2)
    {
        cout<<"usage: ./ttdb <source file>"<<endl;
        return 1;
    }
    string error;
    if (!validate(argv[1],error))
    {
        cout<<"error: "<<error<<endl;
        return 1;
    }
    int64_t mainOffset=resolve(argv[1],"resolve.bin",error);
    if (mainOffset== -1)
    {
        cout<<"error: "<<error<<endl;
        return 1;
    }
    Timeline timeline;
    if (!execute("resolve.bin",mainOffset,timeline,error))
    {
        cout<<"error: "<<error<<endl;
        return 1;
    }
    writeTdbg(timeline,"session.tdbg");
    return 0;
}
