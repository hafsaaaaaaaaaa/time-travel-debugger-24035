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
#include <sstream>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
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
const int MAX_STACK_DEPTH = 32;


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

bool readAt(int fd, void *buf, size_t n, int64_t offset)
{
    char *p = (char *)buf;
    size_t done = 0;
    while (done < n)
    {
        ssize_t r = pread(fd, p + done, n - done, offset + done);
        if (r <= 0)
        {
            return false;
        }
        done += r;
    }
    return true;
}
bool takeBytes(const vector<char> &data, size_t &pos, void *out, size_t n)
{
    if (n > data.size() - pos)
    {
        return false;
    }
    memcpy(out, data.data() + pos, n);
    pos += n;
    return true;
}
bool takeInt32(const vector<char> &data, size_t &pos, int32_t &value)
{
    return takeBytes(data, pos, &value, sizeof(value));
}
bool takeString(const vector<char> &data, size_t &pos, string &value)
{
    uint32_t size;
    if (!takeBytes(data, pos, &size, sizeof(size)))
    {
        return false;
    }
    if (size > data.size() - pos)
    {
        return false;
    }
    value.assign(data.data() + pos, size);
    pos += size;
    return true;
}
bool takeVariable(const vector<char> &data, size_t &pos, Variable &v)
{
    return takeString(data, pos, v.name) && takeInt32(data, pos, v.value);
}
bool parseSnapshot(const vector<char> &data, Snapshot &s)
{
    size_t pos = 0;
    int32_t depth;
    if (!takeInt32(data, pos, depth) || depth < 0 || depth > MAX_STACK_DEPTH)
    {
        return false;
    }
    s.stackDepth = depth;
    for (int i = 0; i < depth; i++)
    {
        Frame &f = s.callStack[i];
        if (!takeString(data, pos, f.func_name) || !takeInt32(data, pos, f.argc))
        {
            return false;
        }
        if (f.argc < 0 || f.argc > MAX_VARS_PER_FRAME)
        {
            return false;
        }
        for (int j = 0; j < f.argc; j++)
        {
            if (!takeVariable(data, pos, f.argv[j]))
            {
                return false;
            }
        }
        if (!takeInt32(data, pos, f.returnLine) || !takeInt32(data, pos, f.localCount))
        {
            return false;
        }
        if (f.localCount < 0 || f.localCount > MAX_VARS_PER_FRAME)
        {
            return false;
        }
        for (int j = 0; j < f.localCount; j++)
        {
            if (!takeVariable(data, pos, f.locals[j]))
            {
                return false;
            }
        }
    }
    return pos == data.size();
}

bool openTdbg(const char *path, TdbgReader &r)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0)
    {
        return false;
    }
    struct stat st;
    char hdr[20];
    if (fstat(fd, &st) < 0 || !readAt(fd, hdr, sizeof(hdr), 0))
    {
        close(fd);
        return false;
    }
    TTDBHeader h;
    memcpy(h.magic, hdr, 4);
    memcpy(&h.version, hdr + 4, 4);
    memcpy(&h.stepCount, hdr + 8, 4);
    memcpy(&h.indexOffset, hdr + 12, 8);
    if (memcmp(h.magic, "TTDB", 4) != 0 || h.version != 1 || h.stepCount <= 0 || h.indexOffset < 20 ||
        h.indexOffset + (int64_t)h.stepCount * 8 != (int64_t)st.st_size)
    {
        close(fd);
        return false;
    }
    vector<int64_t> index(h.stepCount);
    if (!readAt(fd, index.data(), index.size() * sizeof(int64_t), h.indexOffset))
    {
        close(fd);
        return false;
    }
    int64_t prev = 20;
    for (int i = 0; i < h.stepCount; i++)
    {
        if (index[i] < prev || index[i] >= h.indexOffset)
        {
            close(fd);
            return false;
        }
        prev = index[i] + 1;
    }
    r.file_index = fd;
    r.header = h;
    r.index = index;
    return true;
}
void closeTdbg(TdbgReader &r)
{
    // Free r.index and close the file.
    vector<int64_t>().swap(r.index);
    if (r.file_index >= 0)
    {
        close(r.file_index);
    }
    r.file_index = -1;
}
Snapshot *readSnapshotAt(TdbgReader &r, int32_t step)
{
    if (step < 0 || step >= r.header.stepCount)
    {
        return nullptr;
    }
    int64_t start = r.index[step];
    int64_t end = r.header.indexOffset;
    if (step + 1 < r.header.stepCount)
    {
        end = r.index[step + 1];
    }
    vector<char> data(end - start);
    if (!readAt(r.file_index, data.data(), data.size(), start))
    {
        return nullptr;
    }
    Snapshot *s = new Snapshot;
    s->stackDepth = 0;
    if (!parseSnapshot(data, *s))
    {
        delete s;
        return nullptr;
    }
    return s;
}

// =============================================================================================
// PART C: THE SESSION IN MEMORY
// There is one DebugSession variable, created in main() and passed to every command by reference.
// =============================================================================================

void initSession(DebugSession &s)
{
   
    s.active_flag = 0;
    s.reader.file_index = -1;
    s.reader.index.clear();
    s.currentStep = 0;
}
void closeSession(DebugSession &s)
{
    // If active, call closeTdbg(s.reader). Then set active = false.
    // Called when the user types quit and when the input ends.
    if (s.active_flag)
    {
        closeTdbg(s.reader);
    }
    s.active_flag = 0;
}

// =============================================================================================
// PART D: THE COMMANDS
// =============================================================================================

void printSnapshot(int32_t step, int32_t totalSteps, const Snapshot &s); // defined in Part E

bool showCurrent(DebugSession &s)
{
    // Read the snapshot at s.currentStep, call printSnapshot, delete the snapshot.
    // If readSnapshotAt returns nullptr, print "damaged .tdbg file" and return false.
    Snapshot *snap = readSnapshotAt(s.reader, (int32_t)s.currentStep);
    if (snap == nullptr)
    {
        cout << "damaged .tdbg file" << endl;
        return false;
    }
    printSnapshot((int32_t)s.currentStep + 1, s.reader.header.stepCount, *snap);
    delete snap;
    return true;
}
bool moveBy(DebugSession &s, int32_t delta)
{
    if (!s.active_flag)
    {
        cout << "no session yet, use: start <source file>" << endl;
        return false;
    }
    int64_t target = s.currentStep + (int64_t)delta;
    if (target < 0 || target >= s.reader.header.stepCount)
    {
        cout << "step out of range, valid steps are 1 to " << s.reader.header.stepCount << endl;
        return false;
    }
    int64_t previous = s.currentStep;
    s.currentStep = target;
    if (!showCurrent(s))
    {
        s.currentStep = previous;
        return false;
    }
    return true;
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
    string tmpPath = string(TDBG_FILE) + ".tmp";
    int sock = connectToHost(LB_HOST.c_str(), LB_PORT);
    if (sock < 0)
    {
        cout << "cannot connect to the load balancer" << endl;
        return false;
    }
    if (!uploadSource(sock, sourcePath.c_str()))
    {
        close(sock);
        cout << "could not send the source file" << endl;
        return false;
    }
    string error;
    bool received = receiveResponse(sock, tmpPath.c_str(), error);
    close(sock);
    if (!received)
    {
        remove(tmpPath.c_str());
        cout << error << endl;
        return false;
    }
    TdbgReader fresh;
    if (!openTdbg(tmpPath.c_str(), fresh))
    {
        remove(tmpPath.c_str());
        cout << "server sent a damaged .tdbg file" << endl;
        return false;
    }
    if (s.active_flag)
    {
        closeTdbg(s.reader);
    }
    s.reader = fresh;
    rename(tmpPath.c_str(), TDBG_FILE);
    s.currentStep = 0;
    s.active_flag = 1;
    return showCurrent(s);
}
bool cmdNext(DebugSession &s)
{
return moveBy(s, 1);
}
bool cmdPrv(DebugSession &s)
{
return moveBy(s, -1);
}
bool cmdJmpNext(DebugSession &s, int32_t n)
{
return moveBy(s, n);
}
bool cmdJmpPrv(DebugSession &s, int32_t n)
{
return moveBy(s, -n);
}

// =============================================================================================
// PART E: PRINTING
// =============================================================================================

void printSnapshot(int32_t step, int32_t totalSteps, const Snapshot &s)
{
    cout << "step " << step << " of " << totalSteps << endl;
    if (s.stackDepth == 0)
    {
        cout << "  (no active frames)" << endl;
        return;
    }
    for (int i = s.stackDepth - 1; i >= 0; i--)
    {
        const Frame &f = s.callStack[i];
        cout << "  " << f.func_name << "(";
        for (int j = 0; j < f.argc; j++)
        {
            if (j > 0)
            {
                cout << ", ";
            }
            cout << f.argv[j].name << "=" << f.argv[j].value;
        }
        cout << ")";
        if (i > 0)
        {
            cout << "  returns to line " << f.returnLine;
        }
        cout << endl;
        if (f.localCount > 0)
        {
            cout << "    locals:";
            for (int j = 0; j < f.localCount; j++)
            {
                cout << " " << f.locals[j].name << "=" << f.locals[j].value;
            }
            cout << endl;
        }
    }
}
void printHelp()
{
    // Print the commands
    cout << "commands:" << endl;
    cout << "  start <source file>   run the program and show step 1" << endl;
    cout << "  next                  go one step forward" << endl;
    cout << "  prv                   go one step back" << endl;
    cout << "  jmp next <n>          go n steps forward" << endl;
    cout << "  jmp prv <n>           go n steps back" << endl;
    cout << "  help                  show this list" << endl;
    cout << "  quit, exit            leave the debugger" << endl;
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
    istringstream in(line);
    vector<string> words;
    string w;
    while (in >> w)
    {
        words.push_back(w);
    }
    if (words.empty())
    {
        return true;
    }
    if (words[0] == "ttdb")
    {
        words.erase(words.begin());
    }
    if (!words.empty())
    {
        if (words[0] == "start" && words.size() == 2)
        {
            return cmdStart(s, words[1]);
        }
        if (words[0] == "next" && words.size() == 1)
        {
            return cmdNext(s);
        }
        if (words[0] == "prv" && words.size() == 1)
        {
            return cmdPrv(s);
        }
        if (words[0] == "jmp" && words.size() == 3 && (words[1] == "next" || words[1] == "prv"))
        {
            bool digits = words[2].size() <= 10;
            for (char c : words[2])
            {
                if (c < '0' || c > '9')
                {
                    digits = false;
                }
            }
            if (digits)
            {
                long long n = stoll(words[2]);
                if (n >= 1 && n <= MAX_JUMP)
                {
                    if (words[1] == "next")
                    {
                        return cmdJmpNext(s, (int32_t)n);
                    }
                    return cmdJmpPrv(s, (int32_t)n);
                }
            }
        }
        if (words[0] == "help" && words.size() == 1)
        {
            printHelp();
            return true;
        }
        if ((words[0] == "quit" || words[0] == "exit") && words.size() == 1)
        {
            quitRequested = true;
            return true;
        }
    }
    cout << "unknown command" << endl;
    printHelp();
    return false;
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

    if (argc >= 3)
    {
        LB_HOST = argv[1];
        LB_PORT = atoi(argv[2]);
    }
    DebugSession session;
    initSession(session);
    cout << "ttdb time-travel debugger" << endl;
    printHelp();
    bool quitRequested = false;
    string line;
    while (true)
    {
        if (quitRequested)
        {
            break;
        }
        cout << PROMPT << flush;
        if (!getline(cin, line))
        {
            break;
        }
        runCommand(session, line, quitRequested);
    }
    closeSession(session);
    return 0;
}