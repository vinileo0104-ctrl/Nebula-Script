#ifndef NEBULA_API_H
#define NEBULA_API_H

#ifdef _WIN32
#ifdef NEBULA_BUILD_DLL
#define NEBULA_API __declspec(dllexport)
#else
#define NEBULA_API __declspec(dllimport)
#endif
#else
#define NEBULA_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

NEBULA_API int nebula_check_syntax(const char* sourcePath);
NEBULA_API int nebula_compile(const char* sourcePath, const char* outputPath);
NEBULA_API int nebula_run_bytecode(const char* bytecodePath);

#ifdef __cplusplus
}
#endif

#endif
