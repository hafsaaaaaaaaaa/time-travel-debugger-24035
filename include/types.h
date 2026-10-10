#pragma once
#include <cstdint>
#include <string>
using namespace std;

const int MAX_VARS_PER_FRAME = 16;
const int MAX_STACK_DEPTH = 32;

struct FuncEntry
{
    string funcName;
    int64_t byteOffsetInResolveBin;
};

struct PendingPatch
{
    int64_t byteOffsetOfOffsetField;
    string targetFuncName;
};

enum TokenType
{
    KEYWORD,
    IDENTIFIER,
    PARAM
};

struct Token
{
    TokenType type;
    string text;
};

struct Variable
{
    string name;
    int32_t value;
};

struct Frame
{
    string func_name;
    int32_t argc;
    Variable argv[MAX_VARS_PER_FRAME];
    int32_t returnLine;
    Variable locals[MAX_VARS_PER_FRAME];
    int32_t localCount;
};

struct Snapshot
{
    Frame callStack[MAX_STACK_DEPTH];
    int32_t stackDepth;
};

struct TimelineNode
{
    Snapshot *data;
    TimelineNode *next;
    TimelineNode *prev;
};

struct TTDBHeader
{
    char magic[4];
    int32_t version;
    int32_t stepCount;
    int64_t indexOffset;
};
