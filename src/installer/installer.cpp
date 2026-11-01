#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <cstdlib>

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#endif

namespace fs = std::filesystem;

// 0. Função para verificar se está a rodar como Administrador no Windows
bool isAdmin() {
#ifdef _WIN32
    BOOL fIsAdmin = FALSE;
    PSID pAdminSid = NULL;
    SID_IDENTIFIER_AUTHORITY NtAuthority = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(&NtAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID,
        DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &pAdminSid)) {
        CheckTokenMembership(NULL, pAdminSid, &fIsAdmin);
        FreeSid(pAdminSid);
    }
    return fIsAdmin == TRUE;
#else
    return true;
#endif
}

// 3. Função para adicionar um caminho ao PATH do sistema via Registro no Windows
bool addToSystemPath(const std::string& newPath) {
#ifdef _WIN32
    HKEY hKey;
    LONG result = RegOpenKeyExA(
        HKEY_LOCAL_MACHINE,
        "SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment",
        0,
        KEY_READ | KEY_WRITE,
        &hKey
    );

    if (result != ERROR_SUCCESS) return false;

    char currentPath[32767];
    DWORD bufferSize = sizeof(currentPath);
    DWORD type = 0;

    result = RegQueryValueExA(hKey, "Path", NULL, &type, reinterpret_cast<LPBYTE>(currentPath), &bufferSize);
    if (result != ERROR_SUCCESS) {
        RegCloseKey(hKey);
        return false;
    }

    std::string pathStr(currentPath);
    if (pathStr.find(newPath) != std::string::npos) {
        std::cout << "[3/4] O caminho ja se encontra no PATH do sistema.\n";
        RegCloseKey(hKey);
        return true;
    }

    std::string updatedPath = pathStr + ";" + newPath;
    result = RegSetValueExA(
        hKey,
        "Path",
        0,
        REG_EXPAND_SZ,
        reinterpret_cast<const BYTE*>(updatedPath.c_str()),
        static_cast<DWORD>(updatedPath.length() + 1)
    );

    RegCloseKey(hKey);

    if (result == ERROR_SUCCESS) {
        // Notifica todas as janelas sobre a alteração nas variáveis de ambiente
        DWORD_PTR dwResult;
        SendMessageTimeoutA(
            HWND_BROADCAST, WM_SETTINGCHANGE, 0,
            reinterpret_cast<LPARAM>("Environment"),
            SMTO_ABORTIFHUNG, 5000, &dwResult
        );
        return true;
    }
    return false;
#else
    return true;
#endif
}

// 4. Função para registar extensões de ficheiros no Windows
bool registerFileExtension(const std::string& ext, const std::string& fileType, const std::string& command) {
#ifdef _WIN32
    HKEY hKey;
    // Registar Extensão -> FileType (ex: .ns -> NebulaScript.Source)
    if (RegCreateKeyExA(HKEY_CLASSES_ROOT, ext.c_str(), 0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
        RegSetValueExA(hKey, "", 0, REG_SZ, reinterpret_cast<const BYTE*>(fileType.c_str()), static_cast<DWORD>(fileType.length() + 1));
        RegCloseKey(hKey);
    } else return false;

    // Registar FileType -> Shell Command (ex: nebula.exe -nbc "%1")
    std::string shellKey = fileType + "\\shell\\open\\command";
    if (RegCreateKeyExA(HKEY_CLASSES_ROOT, shellKey.c_str(), 0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
        RegSetValueExA(hKey, "", 0, REG_SZ, reinterpret_cast<const BYTE*>(command.c_str()), static_cast<DWORD>(command.length() + 1));
        RegCloseKey(hKey);
        return true;
    }
    return false;
#else
    return true;
#endif
}

int main() {
    std::cout << "===================================================\n";
    std::cout << "            Nebula Script - Instalador C++          \n";
    std::cout << "===================================================\n\n";

    // 0. Verificar Privilégios de Administrador
    if (!isAdmin()) {
        std::cerr << "[ERRO] Este instalador precisa de ser executado como Administrador!\n";
        std::cerr << "Clique com o botao direito no executavel e selecione 'Executar como Administrador'.\n\n";
        system("pause");
        return 1;
    }

    fs::path installDir = "C:\\Program Files\\NebulaScript";
    fs::path currentDir = fs::current_path();

    // 1. Criar diretório de destino
    std::cout << "[1/4] A criar pasta em Program Files...\n";
    try {
        if (!fs::exists(installDir)) {
            fs::create_directories(installDir);
        }
    } catch (const fs::filesystem_error& e) {
        std::cerr << "[ERRO] Falha ao criar diretorio: " << e.what() << "\n";
        system("pause");
        return 1;
    }

    // 2. Verificar e copiar os ficheiros
    std::vector<std::string> requiredFiles = {"nebula.exe", "stub.exe"};
    for (const auto& file : requiredFiles) {
        if (!fs::exists(currentDir / file)) {
            std::cerr << "[ERRO] O ficheiro " << file << " nao foi encontrado nesta pasta!\n";
            std::cerr << "Execute o build.bat primeiro.\n\n";
            system("pause");
            return 1;
        }
    }

    std::cout << "[2/4] A copiar binarios...\n";
    try {
        fs::copy_file(currentDir / "nebula.exe", installDir / "nebula.exe", fs::copy_options::overwrite_existing);
        fs::copy_file(currentDir / "stub.exe", installDir / "stub.exe", fs::copy_options::overwrite_existing);
        if (fs::exists(currentDir / "nebula.dll")) {
            fs::copy_file(currentDir / "nebula.dll", installDir / "nebula.dll", fs::copy_options::overwrite_existing);
        }
    } catch (const fs::filesystem_error& e) {
        std::cerr << "[ERRO] Falha ao copiar binarios: " << e.what() << "\n";
        system("pause");
        return 1;
    }

    // 3. Atualizar PATH no Registro
    std::cout << "[3/4] A atualizar o PATH do sistema com seguranca...\n";
    if (!addToSystemPath(installDir.string())) {
        std::cerr << "[AVISO] Nao foi possivel atualizar o PATH do sistema via Registro.\n";
    }

    // 4. Associar Extensões .ns e .cns
    std::cout << "[4/4] A associar extensoes .ns e .cns...\n";
    std::string cmdNs = "\"" + (installDir / "nebula.exe").string() + "\" -nbc \"%1\"";
    std::string cmdCns = "\"" + (installDir / "nebula.exe").string() + "\" -run \"%1\"";

    registerFileExtension(".ns", "NebulaScript.Source", cmdNs);
    registerFileExtension(".cns", "NebulaScript.Binary", cmdCns);

    std::cout << "\n===================================================\n";
    std::cout << "[SUCESSO] Instalacao concluida com sucesso!\n";
    std::cout << "===================================================\n";
    std::cout << "Reinicie o seu terminal ou VS Code para usar o comando 'nebula'.\n\n";

    system("pause");
    return 0;
}