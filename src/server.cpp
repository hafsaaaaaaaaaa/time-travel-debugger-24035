#include <iostream>
#include <string>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "helper.h"
#include "validate.h"
#include "resolve.h"
#include "execute.h"
#include "serialize.h"
using namespace std;

const int IO_BUFFER_SIZE = 64 * 1024;
const int LISTEN_BACKLOG = 16;
const char *UPLOAD_PATH = "upload.bin";
const char *RESOLVE_PATH = "resolve.bin";
const char *TDBG_PATH = "session.tdbg";

bool sendAll(int sock, const char *buf, size_t n)
{
    size_t sent = 0;
    while (sent < n)
    {
        ssize_t r = send(sock, buf + sent, n - sent, MSG_NOSIGNAL);
        if (r <= 0)
        {
            return false;
        }
        sent += r;
    }
    return true;
}
bool sendMessage(int sock, char status, const string &payload)
{
    Crc32 crc;
    crc.update(&status, 1);
    crc.update(payload.data(), payload.size());
    char trailer[4];
    crcToBytes(crc.finish(), trailer);
    return sendAll(sock, &status, 1) && sendAll(sock, payload.data(), payload.size()) && sendAll(sock, trailer, 4);
}
bool sendFile(int sock, const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
    {
        return false;
    }
    char status = 0;
    Crc32 crc;
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
    return sendAll(sock, trailer, 4);
}
bool receiveUpload(int sock, const char *path, string &error)
{
    FILE *out = fopen(path, "wb");
    if (!out)
    {
        error = "server cannot store the upload";
        return false;
    }
    TrailerChecker checker;
    vector<char> buf(IO_BUFFER_SIZE);
    bool gotStatus = false;
    ssize_t r;
    while ((r = recv(sock, buf.data(), buf.size(), 0)) > 0)
    {
        string body = checker.feed(buf.data(), r);
        if (body.empty())
        {
            continue;
        }
        if (!gotStatus)
        {
            gotStatus = true;
            body.erase(0, 1);
        }
        fwrite(body.data(), 1, body.size(), out);
    }
    fclose(out);
    if (r < 0 || !gotStatus || !checker.verify())
    {
        error = "upload checksum mismatch";
        return false;
    }
    return true;
}
void handleClient(int sock)
{
    string error;
    if (!receiveUpload(sock, UPLOAD_PATH, error))
    {
        sendMessage(sock, 1, error);
        return;
    }
    if (!validate(UPLOAD_PATH, error))
    {
        sendMessage(sock, 1, error);
        return;
    }
    int64_t mainOffset = resolve(UPLOAD_PATH, RESOLVE_PATH, error);
    if (mainOffset == -1)
    {
        sendMessage(sock, 1, error);
        return;
    }
    Timeline timeline;
    if (!execute(RESOLVE_PATH, mainOffset, timeline, error))
    {
        sendMessage(sock, 1, error);
        return;
    }
    remove(TDBG_PATH);
    writeTdbg(timeline, TDBG_PATH);
    if (!sendFile(sock, TDBG_PATH))
    {
        sendMessage(sock, 1, "server could not produce the .tdbg file");
    }
}
int main(int argc, char *argv[])
{
    int port = 5001;
    if (argc >= 2)
    {
        port = atoi(argv[1]);
    }
    int listenSock = socket(AF_INET, SOCK_STREAM, 0);
    if (listenSock < 0)
    {
        perror("socket");
        return 1;
    }
    int one = 1;
    setsockopt(listenSock, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (bind(listenSock, (sockaddr *)&addr, sizeof(addr)) < 0)
    {
        perror("bind");
        return 1;
    }
    if (listen(listenSock, LISTEN_BACKLOG) < 0)
    {
        perror("listen");
        return 1;
    }
    while (true)
    {
        int sock = accept(listenSock, NULL, NULL);
        if (sock < 0)
        {
            continue;
        }
        handleClient(sock);
        close(sock);
    }
    return 0;
}
