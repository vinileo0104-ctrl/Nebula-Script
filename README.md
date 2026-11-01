# Nebula Script

Nebula Script é uma linguagem compilada para bytecode, com um compilador e uma
máquina virtual escritos em C++17. O projeto inclui uma CLI para verificar,
compilar e executar scripts, além de uma DLL com API C para integração com
aplicações C/C++.

## Recursos atuais

- Tipos `number`, `float`, `double`, `string` e `bool`.
- Variáveis, funções tipadas, recursão e retorno de valores.
- Condicionais `if/else` e loops `while` e `for`.
- Arrays tipados, acesso e atribuição por índice e `length()`.
- `struct` com campos tipados, criação, acesso e alteração de campos.
- Operadores aritméticos, comparações, igualdade e lógica com curto-circuito.
- Funções nativas `print`, `read` e `sleep`.
- Importação de módulos `.ns` com `use` e interoperabilidade com DLLs via FFI.
- Bytecode NS02 e geração de executáveis autónomos no Windows.

## Requisitos para compilar

O build atual foi preparado para Windows com GCC/MinGW-w64 e `windres`. A DLL
e os executáveis também são linkados com `libffi`. No MSYS2 UCRT64, instale-a
com:

```bash
pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-binutils mingw-w64-ucrt-x86_64-libffi
```

Adicione `C:\msys64\ucrt64\bin` ao `PATH` do Windows e execute `build.bat` na
raiz do projeto. O script gera:

- `nebula.exe`: compilador e CLI;
- `nebula.dll`: biblioteca para integração C/C++;
- `stub.exe`: runtime usado nos executáveis autónomos.

O número de build é incrementado em `build_number.txt`. Para instalar os
binários e associar extensões no Windows, execute `install.bat` como
administrador.

## Usar a CLI

```cmd
nebula -check tests\exemplo.ns
nebula -nbc tests\exemplo.ns
nebula -run tests\exemplo.cns
nebula -build tests\exemplo.ns
```

Use `-o` para escolher o caminho e o nome do arquivo de saída:

```cmd
nebula -nbc tests\exemplo.ns -o build\exemplo.cns
nebula -build tests\exemplo.ns -o dist\exemplo.exe
```

Crie a pasta de destino antes de usar `-o`; a CLI não cria diretórios. Para
compilar vários arquivos fonte no mesmo programa, passe-os juntos ao comando
`-nbc` ou `-build`. O primeiro arquivo é o ponto de entrada e também determina
o nome padrão do arquivo gerado.

Consulte a ajuda completa com `nebula` ou `nebula -help`.

## Exemplo da linguagem

```nebula
struct Pessoa {
    string nome
    number idade
}

fn somar(number: a, number: b) -> number {
    return a + b
}

Pessoa pessoa = Pessoa("Ada", 30)
number resultado = somar(pessoa.idade, 12)

for (number i = 0; i < 3; i = i + 1) {
    print("Ola, " [pessoa.nome])
}

print("Resultado: " [resultado])
```

O programa [tests/exemplo.ns](tests/exemplo.ns) é um smoke test interativo que
cobre os recursos da linguagem sem usar `use`. Ele testa `read()` e espera que
uma linha seja digitada no terminal.

## API da DLL

A API pública está declarada em
[src/common/NebulaApi.h](src/common/NebulaApi.h):

- `nebula_check_syntax(sourcePath)`: analisa e verifica tipos de um fonte;
- `nebula_compile(sourcePath, outputPath)`: compila para bytecode; um caminho
  de saída nulo ou vazio escolhe o nome do fonte com extensão `.cns`;
- `nebula_run_bytecode(bytecodePath)`: executa um arquivo `.cns`.

As funções retornam `0` em caso de sucesso e um valor diferente de zero em caso
de falha. Um exemplo de host que carrega a DLL está em
[src/tools/test_dll.cpp](src/tools/test_dll.cpp).

## Estrutura do projeto

```text
src/
  cli/          Entrada e comandos do compilador
  common/       API pública e versão
  compiler/     Lexer, parser, AST, verificação de tipos e bytecode
  runtime/      Máquina virtual e stub dos executáveis autónomos
  tools/        Ferramentas de teste
  nebula.cpp    Implementação da API da nebula.dll
tests/          Scripts de teste
docs/           Documentação e bibliotecas de exemplo
nebula-vscode/  Extensão de realce sintático para VS Code
```

## Documentação e licença

A documentação adicional está em [docs/index.html](docs/index.html). O projeto
está licenciado sob a [MIT License](LICENSE).
