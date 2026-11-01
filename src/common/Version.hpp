#ifndef VERSION_HPP
#define VERSION_HPP

// O build.bat passa -DNEBULA_BUILD=<numero> para os três binários
// (nebula.exe, nebula.dll e stub.exe). Compilar à mão sem a flag dá build 0.
#ifndef NEBULA_BUILD
#define NEBULA_BUILD 0
#endif

#define NEBULA_VERSION "0.1.0"

#define NEBULA_STR_IMPL(x) #x
#define NEBULA_STR(x) NEBULA_STR_IMPL(x)
#define NEBULA_BUILD_STR NEBULA_STR(NEBULA_BUILD)

#endif // VERSION_HPP