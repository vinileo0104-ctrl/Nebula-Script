#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <cstdio>
#include <filesystem>
#include "Vm.hpp"
#include "../common/Version.hpp"

extern "C" const char NEBULA_STUB_TAG[] = "NSBUILD:" NEBULA_BUILD_STR ";";

#ifdef _WIN32
#include <windows.h>
#endif

// Obtém o caminho absoluto do próprio executável em execução (.exe)
std::string getSelfPath() {
    #ifdef _WIN32
    char buffer[MAX_PATH];
    GetModuleFileNameA(NULL, buffer, MAX_PATH);
    return std::string(buffer);
    #else
    return "";
    #endif
}

#ifdef _WIN32
bool hasDedicatedConsole() {
    DWORD processIds[2];
    return GetConsoleProcessList(processIds, 2) == 1;
}
#endif

int main(int argc, char* argv[]) {
    #ifdef _WIN32
    // Garante suporte a UTF-8 no terminal do Windows
    SetConsoleOutputCP(CP_UTF8);
    #endif

    if (argc > 1 && std::string(argv[1]) == "--nebula-build") {
    std::cout << "[Nebula Runtime] " << NEBULA_STUB_TAG << "\n";
    return 0;
    }

    std::string selfPath = getSelfPath();
    if (selfPath.empty()) {
        std::cerr << "[Erro] Não foi possível determinar o caminho do executável.\n";
        return 1;
    }

    // Abre o próprio executável em modo binário
    std::ifstream file(selfPath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::cerr << "[Erro] Não foi possível abrir o próprio executável: " << selfPath << "\n";
        return 1;
    }

    std::streamsize fileSize = file.tellg();
    
    // O final do ficheiro guarda os metadados do empacotamento:
    // [ Bytecode .cns ] + [ uint32_t bytecodeSize ] + [ 4 bytes marcador "NSBK" ]
    if (fileSize < 8) {
        std::cerr << "[Erro] Este executável não contém nenhum script Nebula embutido.\n";
        return 1;
    }

    // Lê os últimos 4 bytes para verificar o marcador "NSBK" (Nebula Script Bytecode Key)
    file.seekg(fileSize - 4, std::ios::beg);
    char marker[5] = {0};
    file.read(marker, 4);

    if (std::string(marker, 4) != "NSBK") {
        std::cerr << "[Erro] Marcador de embutimento inválido ou ausente. O ficheiro foi corrompido.\n";
        return 1;
    }

    // Lê o tamanho real do bytecode (4 bytes anteriores ao marcador)
    file.seekg(fileSize - 8, std::ios::beg);
    uint32_t bytecodeSize = 0;
    file.read(reinterpret_cast<char*>(&bytecodeSize), sizeof(bytecodeSize));

    // Calcula onde o bytecode começa exatamente no binário
    std::streamsize bytecodeStart = fileSize - 8 - bytecodeSize;
    if (bytecodeStart < 0) {
        std::cerr << "[Erro] Tamanho de bytecode inválido.\n";
        return 1;
    }

    // Extrai os bytes do bytecode para a memória
    file.seekg(bytecodeStart, std::ios::beg);
    std::vector<uint8_t> bytecodeData(bytecodeSize);
    file.read(reinterpret_cast<char*>(bytecodeData.data()), bytecodeSize);
    file.close();

    // Como a tua VM atual (`Vm.hpp`) lê diretamente de um ficheiro físico no disco,
    // criamos um ficheiro temporário seguro para carregar os bytes extraídos.
    std::error_code tempPathError;
    std::filesystem::path tempPath = std::filesystem::temp_directory_path(tempPathError);
    if (tempPathError) {
        std::cerr << "[Erro VM] Não foi possível obter a pasta temporária: "
                  << tempPathError.message() << "\n";
        return 1;
    }

    #ifdef _WIN32
    tempPath /= "nebula_runtime_" + std::to_string(GetCurrentProcessId()) + ".cns";
    #else
    tempPath /= "nebula_runtime.cns";
    #endif
    const std::string tempFilename = tempPath.string();
    {
        std::ofstream tempFile(tempFilename, std::ios::binary);
        if (!tempFile.is_open()) {
            std::cerr << "[Erro VM] Não foi possível criar o bytecode temporário: "
                      << tempFilename << "\n";
            return 1;
        }
        tempFile.write(reinterpret_cast<const char*>(bytecodeData.data()), bytecodeSize);
        if (!tempFile) {
            std::cerr << "[Erro VM] Falha ao gravar o bytecode temporário: "
                      << tempFilename << "\n";
            std::remove(tempFilename.c_str());
            return 1;
        }
    }

    // Inicializa a Máquina Virtual e executa o bytecode embutido
    VM vm;
    const std::filesystem::path executableDirectory =
        std::filesystem::path(selfPath).parent_path();
    int exitCode = 0;
    if (vm.loadFromFile(tempFilename, executableDirectory)) {
        if (!vm.run()) exitCode = 1;
    } else {
        std::cerr << "[Erro VM] Falha ao carregar o bytecode embutido.\n";
        exitCode = 1;
    }

    // Limpa o ficheiro temporário de cache
    std::remove(tempFilename.c_str());

#ifdef _WIN32
    if (hasDedicatedConsole()) {
        std::cout << "\nPressione Enter para fechar esta janela...";
        std::string input;
        std::getline(std::cin, input);
    }
#endif

    return exitCode;
}