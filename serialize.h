#pragma once
#include <cstdio>
#include "types.h"
#include "timeline.h"

void writeHeader(FILE *f, const TTDBHeader &h);
void writeTdbg(Timeline &timeline, const char *tdbgPath);
