#include "stdafx.h"
#include "messagingpipeserver.h"
#include <process.h>

MessagingPipeServer::MessagingPipeServer()
    : m_IoPort()
    , m_Thread()
    , m_dwMaxMessageLength(DEFAULT_MAX_MESSAGE_LENGTH)
{
}

MessagingPipeServer::~MessagingPipeServer()
{
    if (m_IoPort)
        Close();
}

bool MessagingPipeServer::Init(LPCWSTR lpszName, unsigned nInstances, LPSECURITY_ATTRIBUTES lpSA/*=NULL*/, DWORD dwMaxMessageLength/*=DEFAULT_MAX_MESSAGE_LENGTH*/)
{
    // create the completion port
    m_IoPort.Attach(::CreateIoCompletionPort(
        INVALID_HANDLE_VALUE,
        NULL,
        NULL,
        0));

    if (m_IoPort == NULL)
        return false;

    try {
        // create the Connection objects which would queue themselves
        // with the IoPort
        for (unsigned i=0; i<nInstances; i++) {
            ConnectionPtr cp(new Connection(*this));
            if (cp->Init(lpszName, lpSA))
                m_conns.push_back(cp);
        }

        // start worker thread
        unsigned tid = 0;
        m_Thread.Attach((HANDLE)::_beginthreadex(
            NULL,
            0,
            MessagingPipeServer::_PipeServerFunc,
            this,
            0,
            &tid));
        if (m_Thread != NULL) {
            m_dwMaxMessageLength = dwMaxMessageLength;
            return true;
        }

    } catch (...) {
    }

    // something went wrong!
    m_conns.clear();
    if (m_IoPort) m_IoPort.Close();
    return false;
}

void MessagingPipeServer::Close()
{
    if (m_IoPort) {
        ::PostQueuedCompletionStatus(m_IoPort,
            0,
            COMPKEY_QUIT,
            NULL);
        ::WaitForSingleObject(m_Thread, INFINITE);
        m_Thread.Close();
        m_IoPort.Close();
        m_conns.clear();
    }
}

bool MessagingPipeServer::OnAcceptConnection(HANDLE hPipe)
{
    return true;
}

void MessagingPipeServer::OnMessage(const void* pReq, size_t cbReq, std::vector<BYTE>& response)
{
}

unsigned __stdcall MessagingPipeServer::_PipeServerFunc(void* pParam)
{
    reinterpret_cast<MessagingPipeServer*>(pParam)->PipeServerFunc();
    return 0;
}

void MessagingPipeServer::PipeServerFunc()
{
    for (;;) {
        DWORD cbXferred = 0;
        ULONG_PTR ulpCompletionKey= NULL;
        LPOVERLAPPED lpOv = NULL;
        BOOL bSuccess = ::GetQueuedCompletionStatus(m_IoPort,
            &cbXferred,
            &ulpCompletionKey,
            &lpOv,
            INFINITE);

        // handle quit message first
        if (ulpCompletionKey == COMPKEY_QUIT)
            break;

        if (!bSuccess && !lpOv) // can this happen, is quitting be the right action?!
            break;

        Connection* pConn = reinterpret_cast<Connection*>(ulpCompletionKey);
        if (pConn) pConn->OnCompletion(bSuccess, cbXferred);
    }
}

MessagingPipeServer::Connection::Connection(MessagingPipeServer& server)
    : m_server(server)
    , m_pipe(NULL)
    , m_ol()
    , m_state(Connecting)
    , m_buf()
    , m_request()
{
    ::memset(&m_ol, 0, sizeof(OVERLAPPED));
}

MessagingPipeServer::Connection::~Connection()
{
}

bool MessagingPipeServer::Connection::Init(LPCWSTR lpszName, LPSECURITY_ATTRIBUTES lpSA/*=NULL*/)
{
    HANDLE hPipe = ::CreateNamedPipeW( 
            lpszName,                   // pipe name 
            PIPE_ACCESS_DUPLEX |
            FILE_FLAG_OVERLAPPED,       // read/write access 
#ifdef MESSAGE_MODE
            PIPE_TYPE_MESSAGE |         // message type pipe 
            PIPE_READMODE_MESSAGE |     // message-read mode 
#else
            PIPE_TYPE_BYTE |
            PIPE_READMODE_BYTE |
#endif
            PIPE_WAIT |                 // blocking mode 
            PIPE_REJECT_REMOTE_CLIENTS, // only local clients!
            PIPE_UNLIMITED_INSTANCES,   // max. instances  
            4096,                       // output buffer size 
            4096,                       // input buffer size 
            0,                          // client time-out 
            lpSA);                      // default security attribute 

    if (hPipe == INVALID_HANDLE_VALUE)
        return false;

    if (!::CreateIoCompletionPort(hPipe,
        m_server.GetIoPort(),
        (ULONG_PTR)this,
        0)) {
        ::CloseHandle(hPipe);
        return false;
    }

    m_pipe.Attach(hPipe);
    m_buf.resize(512);

    return ConnectToNewClient();
}

void MessagingPipeServer::Connection::OnCompletion(BOOL bSuccess, DWORD cbXferred)
{
    if (!bSuccess) {
        DWORD dwRet = ::GetLastError(); // should be ERROR_BROKEN_PIPE
        dwRet;
        DisconnectAndReconnect();
        return;
    }

    // state transition happens in the first switch block
    switch (m_state) { 
    case Connecting:// finished connecting to client
        if (m_server.OnAcceptConnection(m_pipe))
            m_state = Reading;
        else // connection rejected. disconnect and reconnect
            DisconnectAndReconnect();
        break;
    case Reading:   // buffer received portion of the message
        HandleReadCompletion(cbXferred);
        if (IsCompleteMessageReceived())
            m_state = Writing;
        break;
    case Writing:   // finished writing, go back to reading state
        m_request.clear();
        m_state = Reading;
        break;
    }

    // post transition action takes place in this switch block
    switch (m_state) {
    case Reading:   // read more from client
        if (!ReadFromClient())
            DisconnectAndReconnect();
        break;
    case Writing:   // write response to client
        if (!WriteResponse())
            DisconnectAndReconnect();
        break;
    }
}

// Queues the pipe for incoming client connection which
// will queue an OVERLAPPED with the IOPort. Note that
// if the client called ConnectToNamedPipe() between
// us creating the pipe and calling ConnectNamedPipe()
// ConnectNamedPipe() can return error with error code
// ERROR_PIPE_CONNECTED. In this case we need to queue
// a Read() immediately and treat the pipe as
// connected.
bool MessagingPipeServer::Connection::ConnectToNewClient()
{
    BOOL fConnected = ::ConnectNamedPipe(m_pipe, &m_ol);
    if (fConnected || ::GetLastError() == ERROR_IO_PENDING) {
        // all is OK, waiting for client connection
        m_state = Connecting;
        return true;
    }

    return false;
}

void MessagingPipeServer::Connection::DisconnectAndReconnect()
{
    ::DisconnectNamedPipe(m_pipe);  // will cause any blocking client calls to fail
    m_request.clear();              // clear the earlier request data, if any
    ConnectToNewClient();
}

bool MessagingPipeServer::Connection::ReadFromClient()
{
    DWORD cbRead = 0;
    BOOL fSuccess = ::ReadFile( 
        m_pipe,
        &m_buf[0],
        m_buf.size(),
        &cbRead,
        &m_ol);

    // if read was successful, data is copied to the buffer
    // Since we're using IoPorts, a completion status would've
    // been queued with it handler for which will do the transfer
    // of the contents of this buffer to our m_request vector.
    if (fSuccess || ::GetLastError() == ERROR_IO_PENDING)
        return true;

    // if it gets here, it means error!
    return false;
}

bool MessagingPipeServer::Connection::IsCompleteMessageReceived()
{
    if (m_request.size() > sizeof(DWORD))
        return *(reinterpret_cast<PDWORD>(&m_request[0])) == m_request.size();
    return false;
}

bool MessagingPipeServer::Connection::WriteResponse()
{
    std::vector<BYTE> response;
    m_server.OnMessage(&m_request[sizeof(DWORD)], m_request.size()-sizeof(DWORD), response);

    m_request.resize(response.size()+sizeof(DWORD));
    *(reinterpret_cast<PDWORD>(&m_request[0])) = response.size();
    if (response.size())
        ::memcpy(&m_request[sizeof(DWORD)], &response[0], response.size());

    // queue a write 
    DWORD cbWritten = 0;
    BOOL fSuccess = ::WriteFile(
        m_pipe,
        &m_request[0],
        m_request.size(),
        &cbWritten,
        &m_ol);

    if (fSuccess || ::GetLastError() == ERROR_IO_PENDING)
        return true;

    return false;
}

// append received portion of message to the internal buffer
void MessagingPipeServer::Connection::HandleReadCompletion(DWORD cbRead)
{
    if (cbRead > 0) {
        size_t cbPrev = m_request.size();
        m_request.resize(m_request.size()+cbRead);
        ::memcpy(&m_request[cbPrev], &m_buf[0], cbRead);
    }
}

void MessagingPipeServer::Connection::HandleWriteCompletion(DWORD cbWritten)
{
    DWORD cbResponse = *(reinterpret_cast<PDWORD>(&m_request[0]));

    if (cbResponse >= cbWritten) {
        // entire message written, switch to read for next message
        if (!ReadFromClient())
            DisconnectAndReconnect();
        return;
    }

    // queue another write for the rest of the message
}

MessagingPipeClient::MessagingPipeClient()
    : m_pipe()
{
}

MessagingPipeClient::~MessagingPipeClient()
{
    if (m_pipe) {
        m_pipe.Close();
    }
}

bool MessagingPipeClient::Init(LPCWSTR lpszPipename)
{
    HANDLE hPipe = ::CreateFile( 
           lpszPipename,   // pipe name 
           GENERIC_READ |  // read and write access 
           GENERIC_WRITE, 
           0,              // no sharing 
           NULL,           // default security attributes
           OPEN_EXISTING,  // opens existing pipe 
           0,              // default attributes 
           NULL);          // no template file 

    // Break if the pipe handle is valid. 
    if (hPipe == INVALID_HANDLE_VALUE)
        return false;

    // The pipe connected; change to message-read mode. 
#ifdef MESSAGE_MODE
    DWORD dwMode = PIPE_READMODE_MESSAGE;
#else
    DWORD dwMode = PIPE_TYPE_BYTE|PIPE_READMODE_BYTE; 
#endif
    if (!::SetNamedPipeHandleState( 
        hPipe,    // pipe handle 
        &dwMode,  // new pipe mode 
        NULL,     // don't set maximum bytes 
        NULL)) {   // don't set maximum time 
        ::CloseHandle(hPipe);
        return false;
    }

    m_pipe.Attach(hPipe);

    return true;
}

bool MessagingPipeClient::SendMessage(const void* pMsg, size_t cbMsg, std::vector<BYTE>& response)
{
    std::vector<BYTE> msg(sizeof(DWORD)+cbMsg);
    *(reinterpret_cast<PDWORD>(&msg[0])) = cbMsg+sizeof(DWORD);
    memcpy(&msg[sizeof(DWORD)], pMsg, cbMsg);

    DWORD cbWritten = 0;
    if (!::WriteFile(m_pipe,
        &msg[0],
        msg.size(),
        &cbWritten,
        NULL))
        return false;

    // now read the size of the response
    DWORD cbResponse = 0;
    DWORD cbRead = 0;
    if (!::ReadFile(
        m_pipe,
        &cbResponse,
        sizeof(DWORD),
        &cbRead,
        NULL)) {
        DWORD dwRet = ::GetLastError();
        if (cbResponse > sizeof(DWORD) && dwRet != ERROR_MORE_DATA)
            return false;
    }

    if (cbRead == sizeof(DWORD) && cbResponse > 0) {
        response.resize(cbResponse);
        cbRead = 0;
        if (!::ReadFile(
            m_pipe,
            &response[0],
            response.size(),
            &cbRead,
            NULL))
            return false;
    }

    return true;
}
