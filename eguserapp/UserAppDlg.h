
// UserAppDlg.h : header file
//

#pragma once


// CUserAppDlg dialog
class CUserAppDlg : public CDialogEx
{
// Construction
public:
	CUserAppDlg(CWnd* pParent = NULL);	// standard constructor

// Dialog Data
	enum { IDD = IDD_EGUSERAPP_DIALOG };

	protected:
	virtual void DoDataExchange(CDataExchange* pDX);	// DDX/DDV support


// Implementation
protected:
	HICON m_hIcon;

	// Generated message map functions
	virtual BOOL OnInitDialog();
	afx_msg void OnSysCommand(UINT nID, LPARAM lParam);
	afx_msg void OnPaint();
	afx_msg HCURSOR OnQueryDragIcon();
	DECLARE_MESSAGE_MAP()
public:
    afx_msg void OnTimer(UINT_PTR nIDEvent);
    afx_msg void OnBnClickedStopservice();
};
