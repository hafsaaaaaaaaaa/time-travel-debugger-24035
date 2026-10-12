#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <vector>
#include "execute.h"

bool execute(const string &binPath,int64_t mainOffset,Timeline &timeline,string &error)
{
    FILE *f=fopen(binPath.c_str(),"rb");
    if (f==nullptr){
        error ="cannot open " + binPath;
        return false;
    }
    vector<int64_t> offsets;
    vector<vector<Token>> code;
    while (true)
    {
        int64_t off;
        int32_t size;
        if(fread(&off,sizeof(int64_t),1,f)!= 1){
            break;
        }    
        if (fread(&size,sizeof(int32_t),1, f) != 1){
            break;
        }
        string text;
        text.resize(size);
        fread(&text[0], 1,size,f);

        vector<Token> tokens;
        istringstream iss(text);
        string word;
        while (iss>>word)
        {
            Token tok;
            tok.text=word;
            if (tokens.size()==0){
                tok.type = KEYWORD;
            }
            else if(tokens.size()==1){
                tok.type = IDENTIFIER;
            }
            else{
                tok.type = PARAM;
            }
            tokens.push_back(tok);
        }
        offsets.push_back(off);
        code.push_back(tokens);
    }
    fclose(f);

    int pc= -1;
    for (size_t i=0;i< offsets.size();i++)
    {
        if (offsets[i]==mainOffset){
            pc =i;
        }
    }
    if (pc== -1)
    {
        error="cannot find main";
        return false;
    }

    Snapshot cur={};
    vector<Token> &mainTokens=code[pc];
    if (mainTokens[0].text !="func"||mainTokens.size()< 2)
    {
        error="main is not a valid func line";
        return false;
    }
    int mainArgs=mainTokens.size() - 2;
    if (mainArgs>MAX_VARS_PER_FRAME)
    {
        error="too many parameters in main";
        return false;
    }
    cur.callStack[0].func_name=mainTokens[1].text;
    cur.callStack[0].argc=mainArgs;
    cur.callStack[0].returnLine= -1;
    cur.callStack[0].localCount= 0;
    for (int i =0; i<mainArgs;i++)
    {
        cur.callStack[0].argv[i].name=mainTokens[i + 2].text;
        cur.callStack[0].argv[i].value =0;
    }
    cur.stackDepth=1;
    pc++;

    while (true)
    {
        if(pc>=(int)code.size()){
            error="program ended before main finished";
            return false;
        }
        vector<Token> &t=code[pc];
        string keyword= t[0].text;
        Frame &fr =cur.callStack[cur.stackDepth - 1];
        int next=pc + 1;

        if (keyword=="set"||keyword=="add"||keyword=="sub"||keyword=="mul"||keyword=="div")
        {
            if (t.size()!= 3){
                error = keyword + " needs a name and a value";
                return false;
            }
            int32_t value=0;
            if ((t[2].text[0]>='0'&& t[2].text[0]<= '9')||t[2].text[0] =='-'){
                value= atoi(t[2].text.c_str());
            }
            else{
                Variable *src=nullptr;
                for (int i=0;i<fr.argc;i++)
                {
                    if (fr.argv[i].name==t[2].text)
                        src= &fr.argv[i];
                }
                for (int i=0;i<fr.localCount;i++)
                { if (fr.locals[i].name==t[2].text)
                        src = &fr.locals[i];
                }
                if (src==nullptr)
                {
                    error="undefined variable: " + t[2].text;
                    return false;
                }
                value=src->value;
            }

            Variable *dest=nullptr;
            for (int i= 0;i<fr.argc; i++)
            {
                if (fr.argv[i].name==t[1].text)
                    dest= &fr.argv[i];
            }
            for (int i=0;i<fr.localCount; i++)
            {
                if (fr.locals[i].name==t[1].text)
                    dest = &fr.locals[i];
            }
            if (dest==nullptr)
            {
                if (keyword !="set")
                {
                    error="undefined variable: " + t[1].text;
                    return false;
                }
                if (fr.localCount>=MAX_VARS_PER_FRAME)
                {
                    error="too many variables in " + fr.func_name;
                    return false;
                }
                dest=&fr.locals[fr.localCount];
                dest->name=t[1].text;
                fr.localCount++;
            }

            if (keyword=="set"){
                dest->value = value;
            }
            else if (keyword=="add"){
                dest->value=dest->value + value;
            }
            else if (keyword=="sub"){
                dest->value=dest->value - value;
            }
            else if (keyword=="mul"){
                dest->value=dest->value * value;
            }
            else{
                if (value==0){
                    error="division by zero";
                    return false;
                }
                dest->value=dest->value / value;
            }
        }
        else if (keyword=="call")
        {
            if (t.size() <2)
            {
                error="call needs a function";
                return false;
            }
            if (cur.stackDepth>= MAX_STACK_DEPTH){
                error="stack overflow";
                return false;
            }

            int64_t target=strtol(t[1].text.c_str(),nullptr, 16);

            int targetIndex = -1;
            for (size_t j=0;j<offsets.size();j++){
                if (offsets[j]==target){
                    targetIndex=j;
                }}
            if (targetIndex== -1)
            {
                error="bad call target";
                return false;
            }
            vector<Token> &ft =code[targetIndex];
            if (ft[0].text!="func" || ft.size()< 2)
            {
                error= "call target is not a function";
                return false;
            }
            int count=ft.size() - 2;
            if (count >MAX_VARS_PER_FRAME){
                error ="too many parameters in " + ft[1].text;
                return false;
            }
            if (count!=(int)t.size() - 2)
            {
                error="wrong number of arguments in call to " + ft[1].text;
                return false;
            }

            Frame &callee=cur.callStack[cur.stackDepth];
            callee.func_name= ft[1].text;
            callee.argc=count;
            callee.returnLine=pc;
            callee.localCount=0;
            for (int i=0;i<count;i++)
            {
                string arg=t[i + 2].text;
                int32_t value=0;
                if ((arg[0]>='0'&&arg[0]<='9') ||arg[0]=='-')
                {
                    value=atoi(arg.c_str());
                }
                else{
                    Variable *src =nullptr;
                    for (int j=0;j<fr.argc;j++){
                        if (fr.argv[j].name==arg)
                            src= &fr.argv[j];
                    }
                    for (int j =0;j<fr.localCount;j++)
                    {
                        if (fr.locals[j].name==arg)
                            src= &fr.locals[j];
                    }
                    if(src==nullptr)
                    {
                        error= "undefined variable: " + arg;
                        return false;
                    }
                    value=src->value;
                }
                callee.argv[i].name=ft[i + 2].text;
                callee.argv[i].value=value;
            }
            cur.stackDepth++;
            next = targetIndex + 1;
        }
        else if(keyword == "func_end")
        {
            if (cur.stackDepth==1)
            {cur.stackDepth=0;
            }
            else
            {
                Frame &caller=cur.callStack[cur.stackDepth - 2];
                vector<Token> &callTokens=code[fr.returnLine];
                for (int i=0;i<fr.argc; i++)
                {
                    Variable *dest=nullptr;
                    for(int j=0; j<caller.argc;j++)
                    {
                        if(caller.argv[j].name==callTokens[i + 2].text){
                            dest= &caller.argv[j];
                        }
                    }
                    for (int j=0; j<caller.localCount;j++)
                    {
                        if (caller.locals[j].name==callTokens[i + 2].text){
                            dest= &caller.locals[j];
                        }
                    }
                    if(dest !=nullptr)
                        dest->value= fr.argv[i].value;
                }
                next = fr.returnLine + 1;
                cur.stackDepth--;
            }
         }
        else
        {
            error="unknown instruction: " + keyword;
            return false;
        }
    Snapshot *s =new Snapshot;
        *s =cur;
        timeline.record(s);
        if (cur.stackDepth==0){
            break;
        }
        pc =next;
    }
    return true;
}
