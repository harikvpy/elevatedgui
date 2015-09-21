
// eguserapp.cpp : Defines the class behaviors for the application.
//

#include "stdafx.h"
#include "eguserapp.h"
#include "UserAppDlg.h"
#include "..\shared\dumptoken.h"
#include "..\shared\MessagingPipeServer.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

using namespace hari;
using namespace std;

// CUserApp

BEGIN_MESSAGE_MAP(CUserApp, CWinApp)
	ON_COMMAND(ID_HELP, &CWinApp::OnHelp)
    ON_BN_CLICKED(IDC_STOPSERVICE, &CUserApp::OnBnClickedStopservice)
END_MESSAGE_MAP()


// CUserApp construction

CUserApp::CUserApp()
    : m_cwdsetter()
    , m_logger(L"eguserapp.log")
    , m_lw("userapp", &m_logger)
{
    m_logger.setLevel(10);
	// support Restart Manager
	m_dwRestartManagerSupportFlags = AFX_RESTART_MANAGER_SUPPORT_RESTART;

	// TODO: add construction code here,
	// Place all significant initialization in InitInstance
    HANDLE hToken = NULL;
    if (::OpenProcessToken(::GetCurrentProcess(), MAXIMUM_ALLOWED, &hToken)) {
        CAccessToken at;
        at.Attach(hToken);

        DumpToken(at, m_lw);
    }
}


// The one and only CUserApp object

CUserApp theApp;


// CUserApp initialization

BOOL CUserApp::InitInstance()
{
	// InitCommonControlsEx() is required on Windows XP if an application
	// manifest specifies use of ComCtl32.dll version 6 or later to enable
	// visual styles.  Otherwise, any window creation will fail.
	INITCOMMONCONTROLSEX InitCtrls;
	InitCtrls.dwSize = sizeof(InitCtrls);
	// Set this to include all the common control classes you want to use
	// in your application.
	InitCtrls.dwICC = ICC_WIN95_CLASSES;
	InitCommonControlsEx(&InitCtrls);

	CWinApp::InitInstance();


	AfxEnableControlContainer();

	// Create the shell manager, in case the dialog contains
	// any shell tree view or shell list view controls.
	CShellManager *pShellManager = new CShellManager;

	// Standard initialization
	// If you are not using these features and wish to reduce the size
	// of your final executable, you should remove from the following
	// the specific initialization routines you do not need
	// Change the registry key under which our settings are stored
	// TODO: You should modify this string to be something appropriate
	// such as the name of your company or organization
	SetRegistryKey(_T("Local AppWizard-Generated Applications"));

	CUserAppDlg dlg;
	m_pMainWnd = &dlg;
	INT_PTR nResponse = dlg.DoModal();
	if (nResponse == IDOK)
	{
		// TODO: Place code here to handle when the dialog is
		//  dismissed with OK
	}
	else if (nResponse == IDCANCEL)
	{
		// TODO: Place code here to handle when the dialog is
		//  dismissed with Cancel
	}

	// Delete the shell manager created above.
	if (pShellManager != NULL)
	{
		delete pShellManager;
	}

	// Since the dialog has been closed, return FALSE so that we exit the
	//  application, rather than start the application's message pump.
	return FALSE;
}

void CUserApp::OnServiceHandle(HANDLE h)
{
    m_hServiceQuit.Attach(h);
    m_lw.getStreamW(1)
        << L"Dumping handle 0x" << std::setw(8) << std::hex << std::setfill(L'0') << endl;
/*
    try {
        DumpToken(m_atService, m_lw);
    } catch (...) {
        m_lw.getStreamW(1)
            << L"Exception dumping handle contents..." << endl;
    }
*/
}

void CUserApp::OnBnClickedStopservice()
{
    // TODO: Add your control notification handler code here
    m_lw.getStreamW(1)
        << L"Stopping the service..." << endl;

    if (m_hServiceQuit) {
        if (!::SetEvent(m_hServiceQuit)) {
            m_lw.getStreamW(1)
                << L"Error setting the service quit event, rc:" << ::GetLastError() << endl;
        }
    }

    MessagingPipeClient mpc;
    
    if (mpc.Init(L"\\\\.\\\\pipe\\_testpipeserver_")) {

        for (int i=0; i<1; i++) {
            std::stringstream ss;
            ss << "stopprot " << "password";
            std::vector<BYTE> resp;
            if (mpc.SendMessage(ss.str().c_str(), ss.str().size(), resp)) {
                if (resp.size() > 0) {
                    CString sResp((char*)&resp[0], resp.size());
                    sResp.Insert(0, L"Reply from server: \"");
                    sResp.Append(L"\"");
                    ::AfxMessageBox(sResp, MB_ICONINFORMATION);
                }
            }
        }
    } else {
        CString s; s.Format(L"Error initializing the client, rc: %d", ::GetLastError());
        ::AfxMessageBox(s);
    }

    /*
    if (m_atService.ImpersonateLoggedOnUser()) {
        m_lw.getStreamW(1)
            << L"Impersonating the service..." << endl;

        HANDLE hEvent = ::OpenEventW(EVENT_MODIFY_STATE, FALSE, L"Global\\__egservice__");
        if (hEvent != NULL) {
            if (!::SetEvent(hEvent)) {
                m_lw.getStreamW(1)
                    << L"Error setting the service quit event, rc:" << ::GetLastError() << endl;
            }
            ::CloseHandle(hEvent);
        } else {
            m_lw.getStreamW(1)
                << L"Error opening the service quit event, rc:" << ::GetLastError() << endl;
        }
        m_atService.Revert();
    } else {
        m_lw.getStreamW(1)
            << L"Error impersonating the service, rc:" << ::GetLastError() << endl;
    }
    */
}
