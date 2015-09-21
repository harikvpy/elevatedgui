// egservice.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include "ConsoleService.h"
#include "logfmwk.h"
#include "..\shared\\dumptoken.h"
#include "..\shared\WorkingDirectorySetter.h"
#include "..\shared\MessagingPipeServer.h"

#include <wtsapi32.h>
#include <userenv.h>
#include <Sddl.h>
#include <Lm.h>

#pragma comment(lib, "wtsapi32.lib")
#pragma comment(lib, "userenv.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "netapi32.lib")

using namespace hari;
using namespace std;

/**
 * RAII class to create/destroy an environment block for a specific
 * user. The user is identified by the supplied token handle.
 */
class EnvironmentBlock {
public:
    EnvironmentBlock(HANDLE hToken) : m_pv(NULL)
    {
        ::CreateEnvironmentBlock(&m_pv, hToken, FALSE);
    }
    ~EnvironmentBlock()
    {
        if (m_pv) ::DestroyEnvironmentBlock(m_pv);
    }
    operator PVOID()
    { return m_pv; }
private:
    PVOID m_pv;
};

/**
 * Represents a token in the Windows Security model.
 */
class Token {

    Token(const Token&);
    Token& operator=(const Token&);

public:
    explicit Token(DWORD dwSessionId)
        : m_hToken(NULL)
    {
        ::WTSQueryUserToken(dwSessionId, &m_hToken);
    }
    ~Token()
    {
        if (m_hToken != NULL && m_hToken != INVALID_HANDLE_VALUE)
            ::CloseHandle(m_hToken);
    }
    operator HANDLE()
    { return m_hToken; }

    /**
     * Returns the process id of the newly created process
     * Or Zero if it failed.
     */
    DWORD CreateProcess(LPCWSTR lpszApplication, 
        LPCWSTR lpszCommandline,
        LPSECURITY_ATTRIBUTES lpProcessAttr=NULL,
        LPSECURITY_ATTRIBUTES lpThreadAttr=NULL)
    {
        std::vector<wchar_t> cmdline;
        if (lpszCommandline) {
            cmdline.resize(::wcslen(lpszCommandline)+1);
            ::wcscpy_s(&cmdline[0], cmdline.size(), lpszCommandline);
        }

        STARTUPINFOW si = {0};
        si.cb = sizeof(si);
        si.lpDesktop = L"winsta0\\default";
        si.wShowWindow = SW_SHOW;

        PROCESS_INFORMATION pi = {0};
        if (::CreateProcessAsUserW(m_hToken,
            lpszApplication,
            cmdline.size() ? &cmdline[0] : NULL,
            lpProcessAttr,
            lpThreadAttr,
            FALSE,
            CREATE_UNICODE_ENVIRONMENT,
            EnvironmentBlock(m_hToken),
            NULL,
            &si,
            &pi)) {
            ::CloseHandle(pi.hProcess);
            ::CloseHandle(pi.hThread);
            return pi.dwProcessId;
        }
        return 0;
    }

private:
    HANDLE m_hToken;
};

// returns a security descriptor that denies the logged on user
// the right to terminate the launched GUI.
PSECURITY_DESCRIPTOR CreateDenyUserTerminateProcessSD()
{
    PSECURITY_ATTRIBUTES pSA = NULL;
    SECURITY_ATTRIBUTES sa = {0};
    PSECURITY_DESCRIPTOR pSD = 0; ULONG ulSD = 0;
    SDDL_INTERACTIVE;
    LPCWSTR lpszSD = L"D:(D;;GW;;;IU)";   // DACL:  Deny, GENERIC_WRTE, Interactive User
    if (::ConvertStringSecurityDescriptorToSecurityDescriptorW(
        lpszSD,
        SDDL_REVISION_1,
        &pSD,
        &ulSD)) {
        return pSD;
    }
    return NULL;
}

class ServiceGroup {

public:
    ServiceGroup(LogWriter& lw)
        : m_fCreated(false)
        , m_lw(lw)
    {
        wchar_t szGroupName[] = L"__egservice__";
        LOCALGROUP_INFO_0 lgi = {0};
        lgi.lgrpi0_name = szGroupName;
        DWORD dwParamErr = 0;
        DWORD dwRet = ::NetLocalGroupAdd(
            NULL,
            0,
            reinterpret_cast<LPBYTE>(&lgi),
            &dwParamErr);
        if (dwRet != NERR_Success) {
            m_lw.getStreamA(1) 
                << "Error creating group account, rc: " << dwRet << endl;
        } else {
            m_fCreated = true;
            m_lw.getStreamA(1) 
                << "Group account \"__egservice__\" created" << endl;
        }
    }

    std::wstring GetSID()
    {
        std::wstring sid;
        DWORD cbSid = 0;
        SID_NAME_USE Type;
        if (::LookupAccountNameW(
            NULL,
            L"__egservice__",
            NULL,
            &cbSid,
            NULL,
            NULL,
            &Type)) {
            std::vector<BYTE> buf(cbSid);
            if (::LookupAccountNameW(
                NULL,
                L"__egservice__",
                (PSID)&buf[0],
                &cbSid,
                NULL,
                NULL,
                &Type)) {
                LPWSTR lpszSid = NULL;
                if (::ConvertSidToStringSidW(reinterpret_cast<PSID>(&buf[0]),
                    &lpszSid)) {
                    sid = lpszSid;
                    ::LocalFree(lpszSid);
                }
            }
        }
        return sid;
    }

    ~ServiceGroup()
    {
        if (m_fCreated) {
            DWORD dwRet = ::NetLocalGroupDel(NULL, L"__egservice__");
            if (dwRet != NERR_Success) {
                m_lw.getStreamA(1) 
                    << "Error deleting group account \"__egservice__\", rc: " << dwRet << endl;
            }
        }
    }

private:
    bool m_fCreated;
    LogWriter& m_lw;
};

class MyPipeServer : public MessagingPipeServer {
public:
    MyPipeServer(Logger& logger)
        : MessagingPipeServer()
        , m_logger(logger)
        , m_lw("mypipeserver", &m_logger)
    {
    }

    bool OnAcceptConnection(HANDLE hPipe);
    void OnMessage(const void* pReq, size_t cbReq, std::vector<BYTE>& response);

private:
    Logger& m_logger;
    LogWriter m_lw;
};

HANDLE g_hQuitEvent = NULL;

BOOL WINAPI CtrlBreakHandler(DWORD dwCtrlType)
{
    if (dwCtrlType == CTRL_C_EVENT) {
        ::SetEvent(g_hQuitEvent);
        return TRUE;
    }
    return FALSE;
}

class MyServiceApp : public hari::ConsoleService<MyServiceApp> {

    typedef hari::ConsoleService<MyServiceApp> baseClass;

    WorkingDirectorySetter m_cwdsetter;
    FileLogger m_logger;
    LogWriter m_lw;
    MyPipeServer m_server;
public:
    MyServiceApp() 
        : baseClass()
        , m_cwdsetter()
        , m_logger(L"egservice.log")
        , m_lw("main", &m_logger)
        , m_server(m_logger)
    {
        m_logger.setLevel(10);
        m_status.dwControlsAccepted |= SERVICE_ACCEPT_SESSIONCHANGE;

        ::CloseHandle(m_hQuitEvent);
        m_hQuitEvent = ::CreateEventW(NULL, TRUE, FALSE, L"Global\\__egservice__");
    }

    virtual DWORD OnSessionChange(DWORD dwEvent, PWTSSESSION_NOTIFICATION pSession)
    {
        switch (dwEvent) {
        case WTS_SESSION_LOGON:
            // user logon
            OnUserLogon(pSession->dwSessionId);
            break;
        case WTS_SESSION_LOGOFF:
            // user logoff
            OnUserLogoff(pSession->dwSessionId);
            break;
        }
        return NO_ERROR;
    }
    void OnUserLogon(DWORD dwSessionId)
    {
        m_lw.getStreamA(1) 
            << "User logon, sessionid: " << dwSessionId << std::endl;

        HANDLE hToken = NULL;
        ::WTSQueryUserToken(dwSessionId, &hToken);
        CAccessToken at;
        at.Attach(hToken);

        CSid sidSG(L"__egservice__");
        CTokenGroups tgRestricted;
        tgRestricted.Add(Sids::Admins(), 0);

        CAccessToken atRestricted;
        atRestricted.Attach(at.Detach());

        if (!atRestricted.SetOwner(Sids::Users())) {
            m_lw.getStreamA(1)
                << "Error setting the owner to SecureGroup, rc: " << ::GetLastError() << endl;
        }

/*
        // can;t launch process with a restricted token that displays itself
        // on the default desktop. This is a limitation of restricted tokens!
        if (at.CreateRestrictedToken(&atRestricted, CTokenGroups(), tgRestricted)) {
            m_lw.getStreamW(1)
                << L"Restricted token created" << endl;
        } else {
            m_lw.getStreamW(1)
                << L"Error creating restricted token, rc: " << GetLastError() << endl;
            return;
        }
*/

//        Token ut(dwSessionId);
#if 0
        // this is purely experimental -- not necessary for our purposes
        PSECURITY_ATTRIBUTES pSA = NULL;
        SECURITY_ATTRIBUTES sa = {0};
        PSECURITY_DESCRIPTOR pSD = CreateDenyUserTerminateProcessSD();
        if (pSD) {
            sa.nLength = sizeof(sa);
            sa.lpSecurityDescriptor = pSD;
            sa.bInheritHandle = FALSE;
            pSA = &sa;
        } else {
            m_lw.getStreamA(1) << "Error creating security descriptor, rc: " << ::GetLastError() << endl;
        }

        //atRestricted.EnablePrivilege;
        if (!::AdjustTokenGroups(
            atRestricted.GetHandle(),
            FALSE,
            const_cast<PTOKEN_GROUPS>(groups.GetPTOKEN_GROUPS()),
            groups.GetLength(),
            NULL,
            0)) {
            m_lw.getStreamA(1)
                << "Error adjusting token groups, rc: " << ::GetLastError() << endl;
        } else {
            m_lw.getStreamA(1)
                << "Token groups adjusted to add service group" << endl;
            DumpToken(atRestricted, m_lw);
        }
#endif

        if (sidSG.IsValid()) {
            m_lw.getStreamW(1)
                << L"Service group sid: " << sidSG.Sid() << endl;

            // default DACL
            CDacl dacl;
            if (atRestricted.GetDefaultDacl(&dacl)) {
                if (dacl.AddAllowedAce(sidSG, ACCESS_ALLOWED_ACE_TYPE)) {
                    if (atRestricted.SetDefaultDacl(dacl)) {
                        m_lw.getStreamA(1)
                            << "Default DACL adjusted to include SERVICEGROUP" << endl;
                    } else {
                        m_lw.getStreamA(1)
                            << "Error setting default DACL, rc: " << ::GetLastError() << endl;
                    }
                } else {
                    m_lw.getStreamA(1)
                        << "Error adding ACE for SERVICEGROUP to default DACL, rc: " << ::GetLastError() << endl;
                }
            } else {
                m_lw.getStreamA(1)
                    << "Error querying default DACL, rc: " << ::GetLastError() << endl;
            }

/*  // this does not work!
            if (!atRestricted.SetPrimaryGroup(sidSG)) {
                m_lw.getStreamA(1)
                    << "Error setting primary group to SERVICEGROUP, rc: " << ::GetLastError() << endl;
            } else {
                m_lw.getStreamA(1)
                    << "Primary group set to SERVICEGROUP" << endl;
            }
*/

        } else {
            m_lw.getStreamA(1)
                << "Group SERVICEGROUP does not exist!" << endl;
        }
        DumpToken(atRestricted, m_lw);

        // create the process in user context
        PROCESS_INFORMATION pi = {0};
        STARTUPINFO si = {0};
        si.cb = sizeof(si);
        si.lpDesktop = L"winsta0\\default";
        si.wShowWindow = SW_SHOW;
        if (!atRestricted.CreateProcessAsUser(
            L"c:\\elevatedgui\\eguserapp.exe", 
            NULL, 
            &pi, 
            &si, 
            CREATE_UNICODE_ENVIRONMENT)) {
            m_lw.getStreamA(1) << "Error creating the user process, rc: " << ::GetLastError() << endl;
        } else {
            m_lw.getStreamA(1) << "User process created!" << endl;
            CAccessToken atThis;
            HANDLE hToken = NULL;
            if (::OpenProcessToken(::OpenProcess(PROCESS_ALL_ACCESS, FALSE, ::GetCurrentProcessId()), TOKEN_ALL_ACCESS, &hToken)) {
                atThis.Attach(hToken);
                ShareHandle(m_hQuitEvent, pi.hProcess);
            }
            ::CloseHandle(pi.hThread);
            ::CloseHandle(pi.hProcess);
        }

        //if (pSD) ::LocalFree(pSD);
    }
    void ShareHandle(HANDLE hHandle, HANDLE hTargetProcess)
    {
        HANDLE hTarget = NULL;
        if (!::DuplicateHandle(::OpenProcess(PROCESS_ALL_ACCESS, FALSE, ::GetCurrentProcessId()),
            hHandle,
            hTargetProcess,
            &hTarget,
            0,
            FALSE,
            DUPLICATE_SAME_ACCESS)) {
            m_lw.getStreamA(1)
                << "Error duplicating handle, rc: " << ::GetLastError() << endl;
            return;
        }

        m_lw.getStreamW(1)
            << L"Handle shared with GUI, GUI handle: 0x" 
            << std::hex << std::setw(8) << std::setfill(L'0') << hTarget << endl;

        ATL::CRegKey key;
        if (key.Create(HKEY_LOCAL_MACHINE,
            L"SOFTWARE\\ShareHandle") == ERROR_SUCCESS) {
            key.SetDWORDValue(L"handle", (DWORD)hTarget);
        }
    }
    void OnUserLogoff(DWORD dwSessionId)
    {
        m_lw.getStreamA(1) 
            << "User logoff, sessionid: " << dwSessionId << std::endl;
    }
    void LaunchGUI()
    {
        HWINSTA hWinsta = ::OpenWindowStationW(L"winsta0", FALSE, WINSTA_ALL_ACCESS);
        if (hWinsta == NULL)
            return;


    }

    void OnUnknownRequest(DWORD dwControl) throw()
    {
        switch (dwControl) {
        case 128:
            OnUserLogon(::WTSGetActiveConsoleSessionId());
            break;
        case 129:
            {
            }
            break;
        }
    }

	DWORD Run()
	{
		// do the service initialization here
		// .........
		// .........

        m_lw.getStreamA(1) 
            << "Starting egservice..." << endl;

        CAccessToken atThis;
        HANDLE hToken = NULL;
        if (::OpenProcessToken(::OpenProcess(PROCESS_ALL_ACCESS, FALSE, ::GetCurrentProcessId()), TOKEN_ALL_ACCESS, &hToken)) {
            atThis.Attach(hToken);
            DumpToken(atThis, m_lw);
        }

        if (!m_server.Init(L"\\\\.\\\\pipe\\_testpipeserver_", 1, NULL, 4096)) {
            m_lw.getStreamA(1) 
                << "Error setting up the pipe server..." << endl;
        }

        //ServiceGroup sg(m_lw);

        //wchar_t szGroupName[] = L"__egservice__";
        //LOCALGROUP_INFO_0 lgi = {0};
        //lgi.lgrpi0_name = szGroupName;
        //DWORD dwParamErr = 0;
        //DWORD dwRet = ::NetLocalGroupAdd(
        //    NULL,
        //    0,
        //    reinterpret_cast<LPBYTE>(&lgi),
        //    &dwParamErr);
        //if (dwRet != NERR_Success) {
        //    m_lw.getStreamA(1) 
        //        << "Error creating group account, rc: " << dwRet << endl;
        //}


        // change service status to running
		SetServiceStatus(SERVICE_RUNNING);
		//m_status.dwCurrentState = SERVICE_RUNNING;
		//::SetServiceStatus(m_hServiceStatus, &m_status);

        if (m_fRunAsNormalProgram) {
            g_hQuitEvent = m_hQuitEvent;
            ::SetConsoleCtrlHandler(CtrlBreakHandler, TRUE);
        }

    	// now wait for the service to quit, set in OnStop()
	    ::WaitForSingleObject(m_hQuitEvent, INFINITE);

		// do the service cleanup
		// .........
		// .........

        m_lw.getStreamA(1) 
            <<  "Stopping egservice..." << endl;

        // stop IPC server
        m_server.Close();

		// change service status to stopped
		SetServiceStatus( SERVICE_STOPPED );

        ATL::CRegKey key;
        if (key.Create(HKEY_LOCAL_MACHINE,
            L"SOFTWARE\\ShareHandle") == ERROR_SUCCESS) {
            key.DeleteValue(L"handle");
        }

		// a return value that will be propagated back to SCM
		return 0;
    }
};

MyServiceApp _app;

struct ClientImpersonator {
    ClientImpersonator(HANDLE hPipe)
    { ::ImpersonateNamedPipeClient(hPipe); }
    ~ClientImpersonator()
    { ::RevertToSelf(); }
};

bool MyPipeServer::OnAcceptConnection(HANDLE hPipe)
{
    DWORD dwProcessId = 0;
    if (GetNamedPipeClientProcessId(hPipe, &dwProcessId)) {
        m_lw.getStreamA(1)
            << "Client process id: " << dwProcessId << endl;
    } else {
        m_lw.getStreamA(1)
            << "Failed to get client process id, rc: " << ::GetLastError() << endl;
    }
    return true;
}

void MyPipeServer::OnMessage(const void* pReq, size_t cbReq, std::vector<BYTE>& response)
{
    std::string s((char*)pReq, cbReq);
    m_lw.getStreamA(1) 
        << "request length: " << cbReq << ", request: " << s.c_str() << endl;

    try {
        std::stringstream ss(s);

        std::string verb;
        ss >> verb;

        if (verb == "stopprot") {
            std::string password;
            ss >> password;
            m_lw.getStreamA(1) 
                << "Request to stop protection, password: " << password.c_str() << endl;
        } else if (verb == "startprot") {
            m_lw.getStreamA(1) 
                << "Request to start protection" << endl;
        } else {
            m_lw.getStreamA(1) 
                << "Unknown request, ignoring..." << endl;
        }

        response.resize(cbReq);
        ::memcpy(&response[0], pReq, cbReq);
    } catch (...) {
        
    }
}

int _tmain(int argc, _TCHAR* argv[])
{
    bool fNormalProgram = false;
    if (argc > 1 && ::_wcsicmp(&argv[1][1], L"debug") == 0)
        fNormalProgram = true;

    return _app.Start(fNormalProgram);
}
