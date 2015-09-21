
// eguserapp.h : main header file for the PROJECT_NAME application
//

#pragma once

#ifndef __AFXWIN_H__
	#error "include 'stdafx.h' before including this file for PCH"
#endif

#include "resource.h"		// main symbols
#include "logfmwk.h"
#include "..\shared\WorkingDirectorySetter.h"


// CUserApp:
// See eguserapp.cpp for the implementation of this class
//

class CUserApp : public CWinApp
{

    WorkingDirectorySetter m_cwdsetter;
    hari::FileLogger m_logger;
    hari::LogWriter m_lw;
    CHandle m_hServiceQuit;
public:
	CUserApp();

// Overrides
public:
	virtual BOOL InitInstance();

    //void DumpToken(CAccessToken& at);

// Implementation

	DECLARE_MESSAGE_MAP()

public:
    void OnServiceHandle(HANDLE h);
    afx_msg void OnBnClickedStopservice();
};

extern CUserApp theApp;