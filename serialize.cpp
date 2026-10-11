#include <vector>
#include "serialize.h"

void writeHeader(FILE *f, const TTDBHeader &h)
{
    fwrite(h.magic, 1, 4, f);
    fwrite(&h.version, sizeof(int32_t), 1, f);
    fwrite(&h.stepCount, sizeof(int32_t), 1, f);
    fwrite(&h.indexOffset, sizeof(int64_t), 1, f);
}
void writeTdbg(Timeline &timeline, const char *tdbgPath)
{
    FILE *f = fopen(tdbgPath, "wb");
    if (f == nullptr){
        return;
    }

    TTDBHeader h;
    h.magic[0] ='T';
    h.magic[1] ='T';
    h.magic[2] ='D';
    h.magic[3] = 'B';
    h.version = 1;
    h.stepCount = timeline.getStepCount();
    h.indexOffset = 0;
    writeHeader(f, h);

    vector<int64_t> index;
    TimelineNode *node=timeline.begin();
    while (node !=nullptr)
    {
        index.push_back(ftell(f));
        fwrite(node->data, sizeof(Snapshot),1,f);
        node = node->next;
    }

    h.indexOffset = ftell(f);
    for (size_t i = 0; i < index.size(); i++){
        fwrite(&index[i], sizeof(int64_t), 1, f);
    }

    fseek(f, 0,SEEK_SET);
    writeHeader(f, h);
    fclose(f);
}
