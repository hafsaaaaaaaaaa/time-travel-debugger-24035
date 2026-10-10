#pragma once
#include "types.h"

class Timeline
{
    TimelineNode *head, *tail;
    int32_t stepCount;

public:
    Timeline();
    void record(Snapshot *s);
    TimelineNode *begin();
    int32_t getStepCount();
};
