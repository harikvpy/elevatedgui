
#pragma once

#include <Windows.h>
#include <atlbase.h>
#include <memory>
#include <list>
#include <vector>

/**
 * A messaging pipe server that uses overlapped I/O to implement
 * efficient multiple client support.
 */
class MessagingPipeServer {

    static const ULONG_PTR COMPKEY_QUIT = 0;

    MessagingPipeServer& operator=(const MessagingPipeServer&);
    MessagingPipeServer(const MessagingPipeServer&);

public:
    MessagingPipeServer();
    virtual ~MessagingPipeServer();

    static const DWORD DEFAULT_MAX_MESSAGE_LENGTH = 1024*1024;  // 1 MB
    bool Init(LPCWSTR lpszName, unsigned nInstances=8, LPSECURITY_ATTRIBUTES lpSA=NULL, DWORD dwMaxMsgSize=DEFAULT_MAX_MESSAGE_LENGTH);
    void Close();

    HANDLE GetIoPort()
    { return m_IoPort; }

private:
    static unsigned __stdcall _PipeServerFunc(void* pParam);
    void PipeServerFunc();

    // represents an instance of a pipe connection
    class Connection : public std::enable_shared_from_this<Connection> {
        friend class MessagingPipeServer;
    public:
        enum State {
            Connecting=0,
            Reading,
            Writing
        };
        Connection(MessagingPipeServer& server);
        ~Connection();

        HANDLE GetPipe()
        { return m_pipe; }

        bool Init(LPCWSTR lpszName, LPSECURITY_ATTRIBUTES lpSA=NULL);

    protected:
        void OnCompletion(BOOL bSuccess, DWORD cbXferred);

    private:
        bool ConnectToNewClient();
        void DisconnectAndReconnect();
        bool ReadFromClient();
        bool WriteResponse();
        bool IsCompleteMessageReceived();
        void HandleReadCompletion(DWORD cbRead);
        void HandleWriteCompletion(DWORD cbWritten);

    private:
        MessagingPipeServer& m_server;
        CHandle m_pipe;
        OVERLAPPED m_ol;
        State m_state;
        std::vector<BYTE> m_buf;
        std::vector<BYTE> m_request;
    };
    typedef std::shared_ptr<Connection> ConnectionPtr;
    friend class Connection;

protected:
    // return true (default) to accept the connection, false to reject
    virtual bool OnAcceptConnection(HANDLE hPipe);
    // incoming request is passed through this
    // request contains the incoming message request
    // response can be sent back through the vector response
    virtual void OnMessage(const void* pReq, size_t cbReq, std::vector<BYTE>& response);

private:
    CHandle m_IoPort;
    CHandle m_Thread;
    std::list<ConnectionPtr> m_conns;
    DWORD m_dwMaxMessageLength;
};

class MessagingPipeClient {

public:
    MessagingPipeClient();
    ~MessagingPipeClient();

    bool Init(LPCWSTR lpszPipename);
    bool SendMessage(const void* pMsg, size_t cbMsg, std::vector<BYTE>& response);

private:
    CHandle m_pipe;
    DWORD m_dwMaxMsgSize;
};

