#include <cstdio>
#include <fstream>
#include <sstream>
#include <vector>
#include "resolve.h"

int64_t findFunc(const vector<FuncEntry> &funcs, const string &name)
{
    for (size_t i = 0; i <funcs.size(); i++)
    {
        if (funcs[i].funcName == name)
            return funcs[i].byteOffsetInResolveBin;
    }
    return -1;}

int64_t resolve(const string &srcPath, const string &binPath, string &error)
{
    ifstream in(srcPath);
    if (!in)
    {
      error = "cannot open " + srcPath;
        return -1;
    }

    FILE *f = fopen(binPath.c_str(), "wb");
    if (f == nullptr)
    {
        error = "cannot create " + binPath;
        return -1;
    }

    vector<FuncEntry> funcs;
    vector<PendingPatch> patches;
    int64_t offset = 0;
    string line;

    while (getline(in, line))
    {
        istringstream iss(line);
        string keyword, name;
        if (!(iss >> keyword)){
            continue;
        }

        string stored = line;
        if (keyword =="func")
        {   iss >> name;
            FuncEntry fe;
            fe.funcName = name;
            fe.byteOffsetInResolveBin = offset;
            funcs.push_back(fe);
        }
        else if (keyword == "call")
        {
            iss >> name;
            string rest;
            getline(iss, rest);
            stored ="call 0x00000000" + rest;

            PendingPatch p;
            p.byteOffsetOfOffsetField = offset + 12 + 5;
            p.targetFuncName = name;
            patches.push_back(p);
        }
        int32_t size = stored.size();
        fwrite(&offset, sizeof(int64_t), 1, f);
        fwrite(&size, sizeof(int32_t), 1, f);
        fwrite(stored.c_str(), 1, size, f);
        offset += 12 + size;
    }
    fclose(f);

    int64_t mainOffset =findFunc(funcs, "main");
    if (mainOffset== -1)
    { error = "main function not found";
        return -1;
    }

    f = fopen(binPath.c_str(), "r+b");
    for (size_t i = 0; i < patches.size(); i++)
    {
        int64_t target = findFunc(funcs, patches[i].targetFuncName);
        if (target == -1)
        {
            fclose(f);
            error = "call to undefined function: " + patches[i].targetFuncName;
            return -1;
        }
        char text[11];
        snprintf(text, sizeof(text), "0x%08x", (unsigned int)target);
        fseek(f, patches[i].byteOffsetOfOffsetField, SEEK_SET);
        fwrite(text, 1, 10, f);
    }
    fclose(f);
    return mainOffset;
}
