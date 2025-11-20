#include <windows.h>
#include <urlmon.h>
#include <iostream>
#include <string>
#include <vector>
#include <io.h>
#include <direct.h>
#include <sddl.h>
#include <shlobj.h>
#include <iomanip>

#pragma comment(lib, "urlmon.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")

struct StepInfo {
    std::string name;
    std::string status; // Pending / In progress / Installed / Skipped (not required) / Skipped (incompatible) / Error
};

BOOL IsRunAsAdmin() {
    BOOL fIsRunAsAdmin = FALSE;
    PSID pAdministratorsGroup = NULL;
    SID_IDENTIFIER_AUTHORITY NtAuthority = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(&NtAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID,
        DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &pAdministratorsGroup)) {
        CheckTokenMembership(NULL, pAdministratorsGroup, &fIsRunAsAdmin);
        FreeSid(pAdministratorsGroup);
    }
    return fIsRunAsAdmin;
}

void Elevate() {
    wchar_t szPath[MAX_PATH];
    GetModuleFileNameW(NULL, szPath, MAX_PATH);
    SHELLEXECUTEINFOW sei = { sizeof(sei) };
    sei.lpVerb = L"runas";
    sei.lpFile = szPath;
    sei.nShow = SW_NORMAL;
    ShellExecuteExW(&sei);
    exit(0);
}

std::wstring s2ws(const std::string& s) {
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring ws(len, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &ws[0], len);
    return ws;
}

bool FileExists(const std::wstring& path) {
    return _waccess(path.c_str(), 0) == 0;
}

void CreateFolder(const std::wstring& path) {
    _wmkdir(path.c_str());
}

// Clear whole console and move cursor to 0,0
void ClearScreen() {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut == INVALID_HANDLE_VALUE) return;

    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (!GetConsoleScreenBufferInfo(hOut, &csbi)) return;

    DWORD size = csbi.dwSize.X * csbi.dwSize.Y;
    DWORD written;
    COORD home = { 0, 0 };

    FillConsoleOutputCharacterW(hOut, L' ', size, home, &written);
    FillConsoleOutputAttribute(hOut, csbi.wAttributes, size, home, &written);
    SetConsoleCursorPosition(hOut, home);
}

// Run process silently and wait
DWORD RunProcess(const std::wstring& executable, const std::wstring& arguments = L"") {
    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = L"open";
    sei.lpFile = executable.c_str();
    sei.lpParameters = arguments.empty() ? nullptr : arguments.c_str();
    sei.nShow = SW_HIDE;

    if (!ShellExecuteExW(&sei)) {
        return GetLastError();
    }

    WaitForSingleObject(sei.hProcess, INFINITE);

    DWORD exitCode = 0;
    GetExitCodeProcess(sei.hProcess, &exitCode);
    CloseHandle(sei.hProcess);

    return exitCode;
}

// Draw UI: header, progress bar, current action, steps list
void RenderUI(
    int overallPercent,
    const std::string& currentAction,
    const std::vector<StepInfo>& steps
) {
    ClearScreen();

    std::cout << "=== Game & Application Components Setup ===\n";
    std::cout << "Installing required components for better compatibility and stability in games and applications...\n";
    std::cout << "Please wait while the setup prepares everything.\n\n";

    if (overallPercent < 0) overallPercent = 0;
    if (overallPercent > 100) overallPercent = 100;

    int barWidth = 50;
    int pos = (overallPercent * barWidth) / 100;
    if (pos > barWidth) pos = barWidth;

    std::cout << "[";
    for (int i = 0; i < barWidth; ++i) {
        if (i < pos) std::cout << "=";
        else std::cout << " ";
    }
    std::cout << "] " << std::setw(3) << overallPercent << "%\n\n";

    std::cout << "Current action: " << currentAction << "\n\n";

    std::cout << "Steps:\n";
    for (const auto& s : steps) {
        std::cout << "  - [" << s.status << "] " << s.name << "\n";
    }

    std::cout.flush();
}

// Simple downloader without spam in console
HRESULT DownloadFileSimple(const std::wstring& url, const std::wstring& path) {
    if (FileExists(path)) {
        return S_OK;
    }
    return URLDownloadToFileW(NULL, url.c_str(), path.c_str(), 0, nullptr);
}

// Step 1: VC++ 2005-2010
bool InstallVC2005_2010_FromZip(const std::wstring& tempDir) {
    std::wstring zipPath = tempDir + L"\\2005-2010.zip";
    std::wstring extractDir = tempDir + L"\\2005-2010_extracted";

    if (FAILED(DownloadFileSimple(L"https://archive.org/download/Redistributable/2005-2010.zip", zipPath))) {
        return false;
    }

    if (!FileExists(extractDir)) {
        CreateFolder(extractDir);
        DWORD code = RunProcess(
            L"powershell.exe",
            L"-Command \"Expand-Archive -Path '" + zipPath + L"' -DestinationPath '" + extractDir + L"' -Force\""
        );
        if (code != 0) {
            return false;
        }
    }

    RunProcess(L"msiexec.exe",
        L"/i \"" + extractDir + L"\\2005_x64\\vcredist.msi\" /qn /norestart");
    RunProcess(L"msiexec.exe",
        L"/i \"" + extractDir + L"\\2005_x86\\vcredist.msi\" /qn /norestart");

    RunProcess(extractDir + L"\\2008_x64\\install.exe", L"/q");
    RunProcess(extractDir + L"\\2008_x86\\install.exe", L"/q");

    RunProcess(L"msiexec.exe",
        L"/i \"" + extractDir + L"\\2010_x64\\vc_red.msi\" /qn /norestart");
    RunProcess(L"msiexec.exe",
        L"/i \"" + extractDir + L"\\2010_x86\\vc_red.msi\" /qn /norestart");

    return true;
}

// Step 2: VC++ 2012+
bool InstallVC2012Plus(const std::wstring& tempDir) {
    std::vector<std::pair<std::string, std::wstring>> packages = {
        {"2012_x86.exe",       L"https://archive.org/download/Redistributable/2012_x86.exe"},
        {"2012_x64.exe",       L"https://archive.org/download/Redistributable/2012_x64.exe"},
        {"2013_x86.exe",       L"https://archive.org/download/Redistributable/2013_x86.exe"},
        {"2013_x64.exe",       L"https://archive.org/download/Redistributable/2013_x64.exe"},
        {"2015_x86.exe",       L"https://archive.org/download/Redistributable/2015_x86.exe"},
        {"2015_x64.exe",       L"https://archive.org/download/Redistributable/2015_x64.exe"},
        {"2017-2026_x86.exe",  L"https://archive.org/download/Redistributable/2017-2026_x86.exe"},
        {"2017-2026_x64.exe",  L"https://archive.org/download/Redistributable/2017-2026_x64.exe"}
    };

    bool anyOk = false;

    for (const auto& pkg : packages) {
        std::wstring fileName = s2ws(pkg.first);
        std::wstring filePath = tempDir + L"\\" + fileName;

        if (FAILED(DownloadFileSimple(pkg.second, filePath))) {
            continue;
        }

        DWORD exitCode = RunProcess(filePath, L"/install /quiet /norestart");

        // 0 – success, 1638/3010 – already installed / no changes / reboot required
        if (exitCode == 0 || exitCode == 1638 || exitCode == 3010) {
            anyOk = true;
        }
    }

    return anyOk;
}

// Step 3: DirectX Runtime
bool InstallDirectX(const std::wstring& tempDir) {
    std::wstring dxFile = tempDir + L"\\dxwebsetup.exe";

    if (FAILED(DownloadFileSimple(
        L"https://archive.org/download/Redistributable/dxwebsetup.exe",
        dxFile
    ))) return false;

    if (!FileExists(dxFile)) return false;

    DWORD code = RunProcess(dxFile, L"/Q");

    // 0 — success, 3010 — reboot required / partial but ok
    if (code == 0 || code == 3010) return true;

    return false;
}

// Step 4: OpenAL Audio
bool InstallOpenAL(const std::wstring& tempDir) {
    std::wstring oalFile = tempDir + L"\\oalinst.exe";

    if (FAILED(DownloadFileSimple(
        L"https://archive.org/download/Redistributable/oalinst.exe",
        oalFile
    ))) return false;

    if (!FileExists(oalFile)) return false;

    DWORD code = RunProcess(oalFile, L"/s");

    return (code == 0);
}

// Step 5: NVIDIA PhysX Legacy (обязателен)
bool InstallPhysXLegacy(const std::wstring& tempDir) {
    std::wstring physxLegacyFile = tempDir + L"\\PhysX-9.13.0604-SystemSoftware-Legacy.msi";

    if (FAILED(DownloadFileSimple(
        L"https://archive.org/download/Redistributable/PhysX-9.13.0604-SystemSoftware-Legacy.msi",
        physxLegacyFile
    ))) return false;

    if (!FileExists(physxLegacyFile)) return false;

    DWORD code = RunProcess(
        L"msiexec.exe",
        L"/i \"" + physxLegacyFile + L"\" /qn /norestart"
    );

    // 0 – ок, 3010 – поставилось, но просит перезагрузку
    return (code == 0 || code == 3010);
}

// Step 6: NVIDIA PhysX (new, optional)
bool InstallPhysXNew(const std::wstring& tempDir, bool& incompatibleOut) {
    incompatibleOut = false;

    std::wstring physxFile = tempDir + L"\\PhysX_9.23.1019_SystemSoftware.exe";

    if (FAILED(DownloadFileSimple(
        L"https://archive.org/download/Redistributable/PhysX_9.23.1019_SystemSoftware.exe",
        physxFile
    ))) {
        // Не скачалось — считаем просто не установленным
        incompatibleOut = true;
        return false;
    }

    if (!FileExists(physxFile)) {
        incompatibleOut = true;
        return false;
    }

    DWORD code = RunProcess(physxFile, L"/s");

    // 0 / 3010 — норм
    if (code == 0 || code == 3010) {
        return true;
    }

    // Любой другой код для нас — "несовместимо/не требуется"
    incompatibleOut = true;
    return false;
}

// Step 7: nGlide 2.10 (NSIS, тихая установка)
bool InstallNGlide(const std::wstring& tempDir) {
    std::wstring nglideFile = tempDir + L"\\nGlide210_setup.exe";

    if (FAILED(DownloadFileSimple(
        L"https://archive.org/download/Redistributable/nGlide210_setup.exe",
        nglideFile
    ))) return false;

    if (!FileExists(nglideFile)) return false;

    // NSIS silent mode
    DWORD code = RunProcess(nglideFile, L"/S");

    return (code == 0);
}

int main() {
    // Steps list
    std::vector<StepInfo> steps = {
        {"Visual C++ 2005-2010",        "Pending"},
        {"Visual C++ 2012 and newer",   "Pending"},
        {"DirectX Runtime",             "Pending"},
        {"OpenAL Audio",                "Pending"},
        {"NVIDIA PhysX Legacy",         "Pending"},
        {"NVIDIA PhysX (new)",          "Pending"},
        {"nGlide 2.10",                 "Pending"}
    };

    int overallPercent = 0;
    std::string currentAction = "Initializing...";

    RenderUI(overallPercent, currentAction, steps);

    if (!IsRunAsAdmin()) {
        currentAction = "Requesting administrator privileges...";
        RenderUI(overallPercent, currentAction, steps);
        Elevate();
    }

    wchar_t temp[MAX_PATH];
    GetTempPathW(MAX_PATH, temp);
    std::wstring tempDir = std::wstring(temp) + L"VC_Redist";
    CreateFolder(tempDir);

    // STEP 1 – VC++ 2005–2010
    steps[0].status = "In progress";
    currentAction = "Installing Visual C++ 2005-2010...";
    overallPercent = 5;
    RenderUI(overallPercent, currentAction, steps);

    if (InstallVC2005_2010_FromZip(tempDir)) {
        steps[0].status = "Installed";
    }
    else {
        steps[0].status = "Error";
    }
    overallPercent = 20;
    currentAction = "Step 1 completed.";
    RenderUI(overallPercent, currentAction, steps);

    // STEP 2 – VC++ 2012+
    steps[1].status = "In progress";
    currentAction = "Installing Visual C++ 2012 and newer...";
    overallPercent = 25;
    RenderUI(overallPercent, currentAction, steps);

    if (InstallVC2012Plus(tempDir)) {
        steps[1].status = "Installed";
    }
    else {
        steps[1].status = "Error";
    }
    overallPercent = 40;
    currentAction = "Step 2 completed.";
    RenderUI(overallPercent, currentAction, steps);

    // STEP 3 – DirectX Runtime (optional)
    steps[2].status = "In progress";
    currentAction = "Installing DirectX Runtime...";
    overallPercent = 45;
    RenderUI(overallPercent, currentAction, steps);

    if (InstallDirectX(tempDir)) {
        steps[2].status = "Installed";
    }
    else {
        steps[2].status = "Skipped (not required)";
    }
    overallPercent = 55;
    currentAction = "DirectX step completed.";
    RenderUI(overallPercent, currentAction, steps);

    // STEP 4 – OpenAL Audio (optional)
    steps[3].status = "In progress";
    currentAction = "Installing OpenAL Audio...";
    overallPercent = 60;
    RenderUI(overallPercent, currentAction, steps);

    if (InstallOpenAL(tempDir)) {
        steps[3].status = "Installed";
    }
    else {
        steps[3].status = "Skipped (not required)";
    }
    overallPercent = 70;
    currentAction = "OpenAL step completed.";
    RenderUI(overallPercent, currentAction, steps);

    // STEP 5 – NVIDIA PhysX Legacy (required)
    steps[4].status = "In progress";
    currentAction = "Installing NVIDIA PhysX Legacy...";
    overallPercent = 75;
    RenderUI(overallPercent, currentAction, steps);

    if (InstallPhysXLegacy(tempDir)) {
        steps[4].status = "Installed";
    }
    else {
        steps[4].status = "Error";
    }
    overallPercent = 85;
    currentAction = "PhysX Legacy step completed.";
    RenderUI(overallPercent, currentAction, steps);

    // STEP 6 – NVIDIA PhysX (new, optional)
    steps[5].status = "In progress";
    currentAction = "Installing NVIDIA PhysX (new)...";
    overallPercent = 88;
    RenderUI(overallPercent, currentAction, steps);

    bool physxNewIncompatible = false;
    if (InstallPhysXNew(tempDir, physxNewIncompatible)) {
        steps[5].status = "Installed";
    }
    else {
        if (physxNewIncompatible) {
            steps[5].status = "Skipped (incompatible / not required)";
        }
        else {
            steps[5].status = "Error";
        }
    }

    // STEP 7 – nGlide 2.10
    steps[6].status = "In progress";
    currentAction = "Installing nGlide 2.10...";
    overallPercent = 92;
    RenderUI(overallPercent, currentAction, steps);

    if (InstallNGlide(tempDir)) {
        steps[6].status = "Installed";
    }
    else {
        steps[6].status = "Error";
    }

    overallPercent = 100;
    currentAction = "All steps completed.";
    RenderUI(overallPercent, currentAction, steps);

    std::cout << "\nSetup finished. You can now close this window.\n";
    if (steps[5].status.find("incompatible") != std::string::npos) {
        std::cout << "Note: NVIDIA PhysX (new) was skipped because your hardware or system did not accept this version.\n";
        std::cout << "This component is optional and most games will work fine without it.\n";
    }
    std::cout << "Press any key to exit...";
    system("pause >nul");
    return 0;
}
