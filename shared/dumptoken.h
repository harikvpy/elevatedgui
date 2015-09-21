#pragma once
#include <atlbase.h>
#include <atlsecurity.h>
#include "logfmwk.h"
#include <iostream>
#include <iomanip>

void DumpToken(CAccessToken& at, hari::LogWriter& lw)
{
    CTokenGroups groups;
    at.GetGroups(&groups);

    CSid sidUser; at.GetUser(&sidUser);
    lw.getStreamW(1)
        << L"\tUser: " << sidUser.AccountName() << L"(" << sidUser.Sid() << L")" << std::endl;

    CSid sidOwner; at.GetOwner(&sidOwner);
    lw.getStreamW(1)
        << L"\tOwner: " << sidOwner.AccountName() << L"(" << sidOwner.Sid() << L")" << std::endl;
/*    
    CSid sidLogon; at.GetLogonSid(&sidLogon);
    lw.getStreamW(1)
        << L"\tLogon Sid: " << sidLogon.Sid() << std::endl;
*/
    lw.getStreamA(1)
        << "\tRestricted token: " << (at.IsTokenRestricted() ? "YES" : "NO") << std::endl;

    CSid sidPrimaryGroup;
    at.GetPrimaryGroup(&sidPrimaryGroup);
    lw.getStreamW(1)
        << L"\tDefault primary group: " << sidPrimaryGroup.AccountName() << L"(" << sidPrimaryGroup.Sid() << L")" << std::endl;

    lw.getStreamA(1)
        << "\tDefault DACL:-" << std::endl;
    CDacl dacl;
    if (at.GetDefaultDacl(&dacl)) {
        CSid::CSidArray sids; CAcl::CAccessMaskArray am;
        dacl.GetAclEntries(&sids, &am);

        for (size_t i=0; i<sids.GetCount(); i++) {
            lw.getStreamW(1)
                << L"\t\tsid: " << sids.GetAt(i).Sid() << " = 0x" 
                << std::hex << std::setw(8) << std::setfill(L'0') << am.GetAt(i) 
                << std::endl;
        }
    }

    CTokenPrivileges priv;
    if (at.GetPrivileges(&priv)) {
        CTokenPrivileges::CNames names;
        priv.GetDisplayNames(&names);
        lw.getStreamW(1)
            << L"\tPrivileges:-" << std::endl;
        for (size_t i=0; i<names.GetCount(); i++)
            lw.getStreamW(1)
                << L"\t\t" << i << L": " << names.GetAt(i).operator LPCWSTR() << std::endl;
    } else {
        
    }

    lw.getStreamW(1)
        << L"\tSids in user token group:- " << std::endl;
    CSid::CSidArray sids;
    groups.GetSidsAndAttributes(&sids);
    for (size_t i=0; i<sids.GetCount(); i++) {
        LPCTSTR lpszSid = sids.GetAt(i).Sid();
        lw.getStreamW(1) << L"\t\t" << sids.GetAt(i).AccountName() << L"(" << lpszSid << L")" << std::endl;
    }
}
