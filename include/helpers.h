#ifndef DIRECTX12_HELPERS_H
#define DIRECTX12_HELPERS_H

#include <string>
#include <string_view>
#include <windows.h>

namespace helpers
{
    //~ String related helpers
    _NODISCARD _Check_return_
    std::string ToLowerAscii(std::string s);

    _NODISCARD _Check_return_
     std::wstring ToLowerAscii(std::wstring s);

    _NODISCARD _Check_return_
     std::string  WideToAnsi(_In_ std::wstring_view wstr);

    _NODISCARD _Check_return_
     std::wstring AnsiToWide(_In_ std::string_view str);

    _NODISCARD
    inline std::string WideToAnsi(_In_opt_ const wchar_t* str)
    {
        return WideToAnsi(std::wstring_view(str ? str : L""));
    }

    _NODISCARD
    inline std::wstring AnsiToWide(_In_opt_ const char* str)
    {
        return AnsiToWide(std::string_view(str ? str : ""));
    }

    _NODISCARD
    inline std::string WideToAnsi(_In_ const std::wstring& wstr)
    {
        return WideToAnsi(std::wstring_view(wstr));
    }

    _NODISCARD
    inline std::wstring AnsiToWide(_In_ const std::string& str)
    {
        return AnsiToWide(std::string_view(str));
    }

    //~ File Utilities
    _NODISCARD _Check_return_
     bool IsPathExists(_In_ const std::wstring& path);

    _NODISCARD _Check_return_
     bool IsPathExists(_In_ const std::string& path);

    _NODISCARD _Check_return_
     bool IsDirectory (_In_ const std::string& path);

    _NODISCARD _Check_return_
     bool IsFile      (_In_ const std::string& path);

    _NODISCARD _Check_return_
    bool IsFile      (_In_ const std::wstring& path);

    _NODISCARD _Check_return_
     bool CopyFiles(_In_ const std::string& source,
                           _In_ const std::string& destination,
                           _In_ bool               overwrite = true);

    _NODISCARD _Check_return_
     bool MoveFiles(_In_ const std::string& source,
                           _In_ const std::string& destination);

    typedef struct DIRECTORY_AND_FILE_NAME
    {
        _In_ std::string DirectoryNames;
        _In_ std::string FileName;
    } DIRECTORY_AND_FILE_NAME;

    _NODISCARD _Check_return_
     DIRECTORY_AND_FILE_NAME SplitPathFile(_In_ const std::string& fullPath);

    template<typename... Args>
    _NODISCARD _Check_return_ __forceinline
    bool DeleteFiles(Args&&... args)
    {
        bool allSuccess = true;

        auto tryDelete = [&](const auto& path)
            {
                std::wstring w_path(path.begin(), path.end());
                if (!DeleteFileW(w_path.c_str()))
                    allSuccess = false;
            };

        (tryDelete(std::forward<Args>(args)), ...);
        return allSuccess;
    }

    template<typename... Args>
    _NODISCARD _Check_return_ __forceinline
    bool CreateDirectories(Args&&... args)
    {
        bool allSuccess = true;

        auto tryCreate = [&](const auto& pathStr)
            {
                std::wstring w_path(pathStr.begin(), pathStr.end());

                std::wstring current;
                for (size_t i = 0; i < w_path.length(); ++i)
                {
                    wchar_t ch = w_path[i];
                    current += ch;

                    if (ch == L'\\' || ch == L'/')
                    {
                        if (!current.empty() && !IsPathExists(current))
                        {
                            if (!CreateDirectoryW(current.c_str(), nullptr) &&
                                GetLastError() != ERROR_ALREADY_EXISTS)
                            {
                                allSuccess = false;
                                return;
                            }
                        }
                    }
                }

                if (!IsPathExists(current))
                {
                    if (!CreateDirectoryW(current.c_str(), nullptr) &&
                        GetLastError() != ERROR_ALREADY_EXISTS)
                    {
                        allSuccess = false;
                    }
                }
            };

        (tryCreate(std::forward<Args>(args)), ...);
        return allSuccess;
    }

    inline std::uint32_t AlignTo256(std::uint32_t size) noexcept
    {
        return (size + 255u) & ~255u;
    }
}

#endif //DIRECTX12_HELPERS_H