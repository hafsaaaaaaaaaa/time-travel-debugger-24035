#include "timeline.h"

Timeline::Timeline()
{
    head = nullptr;
    tail = nullptr;
    stepCount = 0;
}

void Timeline::record(Snapshot *s)
{
    TimelineNode *node = new TimelineNode;
    node->data = s;
    node->next = nullptr;
    node->prev = tail;
  
    if (tail != nullptr){
        tail->next = node;
    }
    else{
        head = node;
    }
    tail = node;
    stepCount++;
}

TimelineNode *Timeline::begin()
{
    return head;
}

int32_t Timeline::getStepCount()
{
    return stepCount;
}
