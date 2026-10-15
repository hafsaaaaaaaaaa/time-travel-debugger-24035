
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include "serialize.h"

static bool writeBytes(FILE *f, const void *data, size_t size)
{
    return fwrite(data, 1, size, f) == size;
}

static bool writeInt32(FILE *f, int32_t value)
{
    return writeBytes(f, &value, sizeof(value));
}

static bool writeString(FILE *f, const string &value)
{
    if (value.size() > UINT32_MAX)
        return false;

    uint32_t size = static_cast<uint32_t>(value.size());

    return writeBytes(f, &size, sizeof(size)) &&
           writeBytes(f, value.data(), size);
}

static bool writeVariable(FILE *f, const Variable &v)
{
    return writeString(f, v.name) &&
           writeInt32(f, v.value);
}

static bool writeSnapshot(FILE *f, const Snapshot &s)
{
    if (s.stackDepth < 0 || s.stackDepth > MAX_STACK_DEPTH)
        return false;

    if (!writeInt32(f, s.stackDepth))
        return false;

    for (int i = 0; i < s.stackDepth; i++)
    {
        const Frame &frame = s.callStack[i];

        if (frame.argc < 0 || frame.argc > MAX_VARS_PER_FRAME ||
            frame.localCount < 0 || frame.localCount > MAX_VARS_PER_FRAME)
            return false;

        if (!writeString(f, frame.func_name) ||
            !writeInt32(f, frame.argc))
            return false;

        for (int j = 0; j < frame.argc; j++)
        {
            if (!writeVariable(f, frame.argv[j]))
                return false;
        }

        if (!writeInt32(f, frame.returnLine) ||
            !writeInt32(f, frame.localCount))
            return false;

        for (int j = 0; j < frame.localCount; j++)
        {
            if (!writeVariable(f, frame.locals[j]))
                return false;
        }
    }

    return true;
}
bool writeHeader(FILE *f, const TTDBHeader &h)
{
    return writeBytes(f, h.magic, 4) &&
           writeInt32(f, h.version) &&
           writeInt32(f, h.stepCount) &&
           writeBytes(f, &h.indexOffset, sizeof(h.indexOffset));
}

void writeTdbg(Timeline &timeline, const char *tdbgPath)
{
    FILE *f = fopen(tdbgPath, "wb");

    if (f == nullptr)
        return;

    TTDBHeader h;
    h.magic[0] = 'T';
    h.magic[1] = 'T';
    h.magic[2] = 'D';
    h.magic[3] = 'B';
    h.version = 1;
    h.stepCount = timeline.getStepCount();
    h.indexOffset = 0;

    bool ok = writeHeader(f, h);

    vector<int64_t> index;
    TimelineNode *node = timeline.begin();

    while (node != nullptr)
    {
        long offset = ftell(f);

        if (offset < 0)
        {
            ok = false;
            break;
        }

        index.push_back(static_cast<int64_t>(offset));

        if (!writeSnapshot(f, *node->data))
        {
            ok = false;
            break;
        }

        node = node->next;
    }

    if (ok && index.size() == static_cast<size_t>(h.stepCount))
    {
        long offset = ftell(f);

        if (offset < 0)
            ok = false;
        else
            h.indexOffset = static_cast<int64_t>(offset);
    }
    else
    {
        ok = false;
    }

    if (ok)
    {
        for (int64_t offset : index)
        {
            if (!writeBytes(f, &offset, sizeof(offset)))
            {
                ok = false;
                break;
            }
        }
    }

    if (ok)
{
    if (fseek(f, 0, SEEK_SET) != 0 || !writeHeader(f, h))
        ok = false;
}

    fclose(f);

    if (!ok)
        remove(tdbgPath);
}
