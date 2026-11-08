// ======================= TIME-TRAVEL DEBUGGER - CLIENT TEMPLATE =======================

// The client never talks to the server directly. It only talks to the load balancer.
// The load balancer wakes the server up if it is down, so the client does not care.
// The first "start" may just take a bit longer.
//
// The network is used ONLY by "start". After the .tdbg file is downloaded,
// next / prv / jmp read that file on the local disk. They never contact the load balancer.
//
// How the pieces fit together:
//   main loop -> readCommand -> runCommand
//   start -> cmdStart()  -> requestDebugFile() (Part A) -> openTdbg() (Part B) -> shows step 1
//   next, prv, jmp -> moveBy() (Part D) -> readSnapshotAt() (Part B) -> printSnapshot() (Part E)

#include <iostream>
#include <string>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <signal.h>
#include <vector>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "helper.h"
using namespace std;

// ---- Constants ----
const char *PROGRAM_NAME = "ttdb";
const char *PROMPT = "ttdb> ";
const int IO_BUFFER_SIZE = 64 * 1024;     // fixed buffer for streaming files to and from the socket
const char *TDBG_FILE = "ttdb_last.tdbg"; // where the downloaded .tdbg is saved
const int MAX_JUMP = 1000000000;          // biggest n accepted by jmp
string LB_HOST = "127.0.0.0";
int LB_PORT = 5000;

// These two must be the same as in Phase 1

const int MAX_VARS_PER_FRAME = 16;
const int MAX_STACK_DEPTH = 64;


// ---- Structs (same as Phase 1, so the file can be read back exactly as it was written) ----
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
struct TTDBHeader 
{
    char magic[4]; // "TTDB"
    int32_t version;
    int32_t stepCount;
    int64_t indexOffset;
};

// ---- Structs only the client needs ----
struct TdbgReader
{
    ssize_t file_index;
    TTDBHeader header;
    vector<int64_t> index;
};
struct DebugSession
{
    int active_flag;     // false until a "start" has worked
    TdbgReader reader;   // the .tdbg file stays open for the whole session
    int64_t currentStep; 
};

// =============================================================================================
// PART A: TALKING TO THE LOAD BALANCER
// =============================================================================================

bool recvAll(int sock, char *buf, size_t n)
{
    size_t got = 0;
    while (got < n)
    {
        ssize_t r = recv(sock, buf + got, n - got, 0);
        if (r <= 0){
            return false;
        }
        got = got + r;
    }
    return true;
}
bool sendAll(int sock, const char *buf, size_t n)
{
    size_t sent = 0;
    while (sent < n)
    {
        ssize_t r = send(sock, buf + sent, n - sent, MSG_NOSIGNAL);
        if (r <= 0){
            return false;
        }
        sent = sent + r;
    }
    return true;
}

int connectToHost(const char *host, int port)
{
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0){
        return -1;
    }
    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1)
    {
        close(sock);
        return -1;
    }
    if (connect(sock, (sockaddr *)&addr, sizeof(addr)) < 0)
    {
        close(sock);
        return -1;
    }
    return sock;
}
bool uploadSource(int sock, const char *sourcePath)
{
    FILE *f = fopen(sourcePath, "rb");
    if (!f){
        return false;
    }
    Crc32 crc;
    char status = 0;
    crc.update(&status, 1);
    if (!sendAll(sock, &status, 1))
    {
        fclose(f);
        return false;
    }
    vector<char> buf(IO_BUFFER_SIZE);
    size_t n;
    while ((n = fread(buf.data(), 1, buf.size(), f)) > 0)
    {
        crc.update(buf.data(), n);
        if (!sendAll(sock, buf.data(), n))
        {
            fclose(f);
            return false;
        }
    }
    fclose(f);
    char trailer[4];
    crcToBytes(crc.finish(), trailer);
    if (!sendAll(sock, trailer, 4)){
        return false;
    }
    shutdown(sock, SHUT_WR);
    return true;
}
bool receiveResponse(int sock, const char *outTdbgPath, string &outError)
{
    TrailerChecker checker;
    vector<char> buf(IO_BUFFER_SIZE);
    FILE *out = NULL;
    bool gotStatus = false;
    char status = 0;
    size_t total = 0;
    ssize_t r;
    while ((r = recv(sock, buf.data(), buf.size(), 0)) > 0)
    {
        total = total + r;
        string body = checker.feed(buf.data(), r);
        if (body.empty())
        {
            continue;
        }
        if (!gotStatus)
        {
            status = body[0];
            body.erase(0, 1);
            gotStatus = true;
            if (status == 0)
            {
                out = fopen(outTdbgPath, "wb");
                if (!out)
                {
                    outError = "cannot write the .tdbg file";
                    return false;
                }
            }
        }
        if (status == 0)
        {
            if (fwrite(body.data(), 1, body.size(), out) != body.size())
            {fclose(out);
                remove(outTdbgPath);
                outError = "cannot write the .tdbg file";
                return false;
            }
        }
        else {
            outError = outError + body;
        }
    }
    if(out)
    {
        fclose(out);
    }
    if (total == 0)
    {
        outError = "connection closed without a reply";
        return false;
    }
    if (r < 0 || !gotStatus|| !checker.verify())
    {
        if (status == 0)
        {remove(outTdbgPath);
        }
        outError = "checksum mismatch, response corrupted";
        return false;
    }
    return status == 0;
}

// =============================================================================================
// PART B: READING THE .tdbg FILE
// File layout (written by Phase 1):
//   header | snapshot 0 | snapshot 1 | ... | index (one 8 byte offset per snapshot)
// The index lets us jump straight to any step without reading the steps before it.
// =============================================================================================

bool openTdbg(const char *path, TdbgReader &r)
{
}
void closeTdbg(TdbgReader &r)
{
    // Free r.index and close the file.
}
Snapshot *readSnapshotAt(TdbgReader &r, int32_t step)
{
}

// =============================================================================================
// PART C: THE SESSION IN MEMORY
// There is one DebugSession variable, created in main() and passed to every command by reference.
// =============================================================================================

void initSession(DebugSession &s)
{
   
}
void closeSession(DebugSession &s)
{
    // If active, call closeTdbg(s.reader). Then set active = false.
    // Called when the user types quit and when the input ends.
}

// =============================================================================================
// PART D: THE COMMANDS
// =============================================================================================

void printSnapshot(int32_t step, int32_t totalSteps, const Snapshot &s); // defined in Part E

bool showCurrent(DebugSession &s)
{
    // Read the snapshot at s.currentStep, call printSnapshot, delete the snapshot.
    // If readSnapshotAt returns nullptr, print "damaged .tdbg file" and return false.
}
bool moveBy(DebugSession &s, int32_t delta)
{
}
bool cmdStart(DebugSession &s, const string &sourcePath)
{
    // 1. requestDebugFile with host and port, saving to TDBG_FILE.
    // 2. If it fails, print the error text and return false. Keep the old session as it was.
    // 3. Open the new file with openTdbg into a NEW local TdbgReader.
    //    If that fails, print "server sent a damaged .tdbg file", keep the old session, return false.
    // 4. Only now close the old reader (if s.active), then put the new reader into s,
    //    set s.currentStep = 0 and s.active = true.
    // 5. Show step 1 with showCurrent(s).
}
bool cmdNext(DebugSession &s) {}
bool cmdPrv(DebugSession &s) {}
bool cmdJmpNext(DebugSession &s, int32_t n) {}
bool cmdJmpPrv(DebugSession &s, int32_t n) { }

// =============================================================================================
// PART E: PRINTING
// =============================================================================================

void printSnapshot(int32_t step, int32_t totalSteps, const Snapshot &s)
{
}
void printHelp()
{
    // Print the commands
}

// =============================================================================================
// PART F: THE INFINITE LOOP
// =============================================================================================

bool runCommand(DebugSession &s, const string &line, bool &quitRequested)
{
    // 1. Split the line into words.
    // 2. If the first word is "ttdb", drop it.
    // 3. Match the first word:
    //      start      -> needs a path after it, then cmdStart
    //      next       -> no more words, then cmdNext
    //      prv        -> no more words, then cmdPrv
    //      jmp        -> second word "next" or "prv", third word a positive number ,
    //                    then cmdJmpNext or cmdJmpPrv
    //      help       -> printHelp
    //      quit, exit -> set quitRequested = true
    // 4. For anything else, or a wrong number of words, print "unknown command" and the help text, and return false.
}
int main(int argc, char *argv[])
{
    // you get string LB_HOST and int LB_PORT = 0 in arguments of this main function. 

    // DebugSession session; initSession(session);
    // Print a short welcome and the help text.
    //
    // Infinite loop:
    //   if quitRequested, break
    //
    // After the loop: closeSession(session) and return 0.
    // Never exit the loop because of an error. Only quit or the end of the input leaves it.

}