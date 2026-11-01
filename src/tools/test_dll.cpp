#include <iostream>
#include <string>
#include <windows.h> // Necessário para carregar DLLs no Windows (LoadLibrary, GetProcAddress)

// Protótipos da API C exposta por nebula.dll.
typedef int (*FuncCheckSyntax)(const char*);
typedef int (*FuncCompile)(const char*, const char*);
typedef int (*FuncRunBytecode)(const char*);

int main(int argc, char* argv[]) {
    const std::string sourcePath = argc > 1 ? argv[1] : "tests\\exemplo.ns";
    const std::string dllPath = argc > 2 ? argv[2] : "nebula.dll";
    const size_t extension = sourcePath.find_last_of('.');
    const std::string bytecodePath =
        extension == std::string::npos
            ? sourcePath + ".cns" : sourcePath.substr(0, extension) + ".cns";

    std::cout << "[Host C++] A carregar a nebula.dll...\n";

    // 1. Carrega a DLL em tempo de execução
    HMODULE hDll = LoadLibraryA(dllPath.c_str());
    if (!hDll) {
        std::cerr << "[Erro] Não foi possível encontrar ou carregar "
                  << dllPath << "!\n";
        return 1;
    }

    // 2. Obtém os ponteiros para as funções exportadas
    auto nebula_check_syntax = reinterpret_cast<FuncCheckSyntax>(
        GetProcAddress(hDll, "nebula_check_syntax"));
    auto nebula_compile = reinterpret_cast<FuncCompile>(
        GetProcAddress(hDll, "nebula_compile"));
    auto nebula_run_bytecode = reinterpret_cast<FuncRunBytecode>(
        GetProcAddress(hDll, "nebula_run_bytecode"));

    if (!nebula_check_syntax || !nebula_compile || !nebula_run_bytecode) {
        std::cerr << "[Erro] Não foi possível encontrar uma ou mais funções na DLL!\n";
        FreeLibrary(hDll);
        return 1;
    }

    std::cout << "[Host C++] DLL carregada com sucesso! A testar '"
              << sourcePath << "'...\n\n";

    // 3. Testa a função de análise de sintaxe da DLL
    int resultadoSintaxe = nebula_check_syntax(sourcePath.c_str());
    std::cout << "[Host C++] Resultado de check_syntax: " << resultadoSintaxe << "\n\n";
    if (resultadoSintaxe != 0) {
        FreeLibrary(hDll);
        return resultadoSintaxe;
    }

    // 4. Testa a função de compilação para bytecode (.cns)
    std::cout << "[Host C++] A compilar o script via DLL...\n";
    int resultadoCompilacao = nebula_compile(sourcePath.c_str(), "");
    
    if (resultadoCompilacao == 0) {
        std::cout << "[Host C++] Compilação bem-sucedida! A executar bytecode...\n";
        // 5. Executa o bytecode gerado
        resultadoCompilacao = nebula_run_bytecode(bytecodePath.c_str());
    } else {
        std::cerr << "[Host C++] Falha na compilação.\n";
    }

    // 6. Liberta a DLL da memória
    FreeLibrary(hDll);
    std::cout << "\n[Host C++] Teste concluído e DLL descarregada.\n";
    
    return resultadoCompilacao;
}