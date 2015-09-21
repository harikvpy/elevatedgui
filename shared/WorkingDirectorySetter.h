#pragma once

/**
 * Retrieves the path of the current executable and stores
 * it in a member variable. The path can be retrieved
 * by casting it LPCWSTR() or through GetPath() member.
 *
 * Ctor should not fail. If it does fail, it probably indicates
 * that the OS is in a critical error state. (Or is it?)
 */
class ExecutablePath {
public:
    ExecutablePath()
    {
        ::memset(m_szPath, 0, sizeof(m_szPath));
        ::GetModuleFileNameW(NULL, m_szPath, _countof(m_szPath));
        wchar_t* pcLastBslash = ::wcsrchr(m_szPath, L'\\');
        *pcLastBslash = L'\0';
    }
    operator LPCWSTR()
    { return m_szPath; }
    const wchar_t* GetPath()
    { return m_szPath; }
private:
    wchar_t m_szPath[MAX_PATH+1];
};

/**
 * A class to set the current working directory to the supplied path.
 * If a path is not supplied, sets the working directory to the
 * location where the executable is resident.
 */
class WorkingDirectorySetter {
public:
    WorkingDirectorySetter(LPCWSTR lpszPath=NULL)
    {
        ExecutablePath ep;
        std::wstring path(lpszPath ? lpszPath : ep.GetPath());
        ::SetCurrentDirectoryW(path.c_str());
    }
};
