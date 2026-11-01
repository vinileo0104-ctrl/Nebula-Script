#ifndef AST_HPP
#define AST_HPP

#include <string>
#include <vector>
#include <memory>
#include <utility>
#include "Token.hpp"

// Declarações antecipadas (Forward declarations)
struct Expr;
struct Stmt;

// ==========================================
// EXPRESSÕES (Expr)
// ==========================================
struct Expr {
    virtual ~Expr() = default;
};

struct ArrayLiteralExpr : public Expr {
    std::vector<std::unique_ptr<Expr>> elements;

    explicit ArrayLiteralExpr(std::vector<std::unique_ptr<Expr>> elems)
        : elements(std::move(elems)) {}
};

struct UnaryExpr : public Expr {
    Token op;
    std::unique_ptr<Expr> right;

    UnaryExpr(Token op, std::unique_ptr<Expr> right)
        : op(std::move(op)), right(std::move(right)) {}
};

struct IndexExpr : public Expr {
    std::unique_ptr<Expr> object;
    std::unique_ptr<Expr> index;

    IndexExpr(std::unique_ptr<Expr> object, std::unique_ptr<Expr> index)
        : object(std::move(object)), index(std::move(index)) {}
};

struct FieldExpr : public Expr {
    std::unique_ptr<Expr> object;
    Token field;

    FieldExpr(std::unique_ptr<Expr> object, Token field)
        : object(std::move(object)), field(std::move(field)) {}
};

struct LiteralExpr : public Expr {
    NsTokenType tokenType; // Atualizado para NsTokenType e tokenType
    std::string value;

    LiteralExpr(NsTokenType tType, std::string val)
        : tokenType(tType), value(std::move(val)) {}
};
// Variáveis (ex: x, y)
struct VariableExpr : public Expr {
    Token name;

    explicit VariableExpr(Token name) : name(std::move(name)) {}
};

// Operações Binárias (ex: y > x, x + 5)
struct BinaryExpr : public Expr {
    std::unique_ptr<Expr> left;
    Token op;
    std::unique_ptr<Expr> right;

    BinaryExpr(std::unique_ptr<Expr> l, Token o, std::unique_ptr<Expr> r)
        : left(std::move(l)), op(std::move(o)), right(std::move(r)) {}
};

// Chamada de Função (ex: Soma(10, 5))
struct CallExpr : public Expr {
    Token callee; // Nome da função chamada
    std::vector<std::unique_ptr<Expr>> arguments;

    CallExpr(Token callee, std::vector<std::unique_ptr<Expr>> args)
        : callee(std::move(callee)), arguments(std::move(args)) {}
};


// ==========================================
// INSTRUÇÕES (Stmt)
// ==========================================
struct Stmt {
    virtual ~Stmt() = default;
};

// Estrutura para Condicionais (ex: if (x > 5) { ... } else { ... })
// MOVIDO PARA CÁ: Agora o compilador já conhece a definição completa de Stmt!
struct IfStmt : public Stmt {
    std::unique_ptr<Expr> condition;
    std::vector<std::unique_ptr<Stmt>> thenBranch;
    std::vector<std::unique_ptr<Stmt>> elseBranch;

    IfStmt(std::unique_ptr<Expr> cond, std::vector<std::unique_ptr<Stmt>> thenB, std::vector<std::unique_ptr<Stmt>> elseB)
        : condition(std::move(cond)), thenBranch(std::move(thenB)), elseBranch(std::move(elseB)) {}
};

// Declaração de Variável (ex: number: x = 10)
struct VarDeclStmt : public Stmt {
    std::string dataType;
    Token name;
    std::unique_ptr<Expr> initializer;

    VarDeclStmt(std::string type, Token name, std::unique_ptr<Expr> init)
        : dataType(std::move(type)), name(std::move(name)), initializer(std::move(init)) {}
};

// Comando Print (ex: print("Valor:" [x]))
struct PrintStmt : public Stmt {
    std::string message;
    std::vector<std::unique_ptr<Expr>> interpolations;

    PrintStmt(std::string msg, std::vector<std::unique_ptr<Expr>> interps)
        : message(std::move(msg)), interpolations(std::move(interps)) {}
};

// Ciclo While (ex: while(y > x) { ... })
struct WhileStmt : public Stmt {
    std::unique_ptr<Expr> condition;
    std::vector<std::unique_ptr<Stmt>> body;

    WhileStmt(std::unique_ptr<Expr> cond, std::vector<std::unique_ptr<Stmt>> b)
        : condition(std::move(cond)), body(std::move(b)) {}
};

struct ForStmt : public Stmt {
    std::unique_ptr<Stmt> initializer;
    std::unique_ptr<Expr> condition;
    std::unique_ptr<Stmt> increment;
    std::vector<std::unique_ptr<Stmt>> body;

    ForStmt(std::unique_ptr<Stmt> initializer, std::unique_ptr<Expr> condition,
            std::unique_ptr<Stmt> increment, std::vector<std::unique_ptr<Stmt>> body)
        : initializer(std::move(initializer)), condition(std::move(condition)),
          increment(std::move(increment)), body(std::move(body)) {}
};

struct StructDeclStmt : public Stmt {
    Token name;
    std::vector<std::pair<std::string, Token>> fields;

    StructDeclStmt(Token name, std::vector<std::pair<std::string, Token>> fields)
        : name(std::move(name)), fields(std::move(fields)) {}
};

// Declaração de Função (ex: fn Nome(number: x) -> number { ... })
struct FunctionDeclStmt : public Stmt {
    Token name;
    std::vector<std::pair<std::string, Token>> parameters; // <tipo, nome>
    std::string returnType;
    std::vector<std::unique_ptr<Stmt>> body;

    FunctionDeclStmt(Token name, std::vector<std::pair<std::string, Token>> params, std::string retType, std::vector<std::unique_ptr<Stmt>> b)
        : name(std::move(name)), parameters(std::move(params)), returnType(std::move(retType)), body(std::move(b)) {}
};

struct ExternFunctionDeclStmt : public Stmt {
    Token name;
    std::vector<std::pair<std::string, Token>> parameters;
    std::string returnType;

    ExternFunctionDeclStmt(Token name, std::vector<std::pair<std::string, Token>> params,
                           std::string retType)
        : name(std::move(name)), parameters(std::move(params)),
          returnType(std::move(retType)) {}
};

struct ExpressionStmt : public Stmt {
    std::unique_ptr<Expr> expression;

    explicit ExpressionStmt(std::unique_ptr<Expr> expr)
        : expression(std::move(expr)) {}
};

struct ArraySetStmt : public Stmt {
    Token name;
    std::unique_ptr<Expr> index;
    std::unique_ptr<Expr> value;

    ArraySetStmt(Token name, std::unique_ptr<Expr> index, std::unique_ptr<Expr> value)
        : name(std::move(name)), index(std::move(index)), value(std::move(value)) {}
};

struct FieldSetStmt : public Stmt {
    Token name;
    Token field;
    std::unique_ptr<Expr> value;

    FieldSetStmt(Token name, Token field, std::unique_ptr<Expr> value)
        : name(std::move(name)), field(std::move(field)), value(std::move(value)) {}
};

// Instrução Return (ex: return y + z)
struct ReturnStmt : public Stmt {
    Token keyword;
    std::unique_ptr<Expr> value;

    ReturnStmt(Token keyword, std::unique_ptr<Expr> val)
        : keyword(std::move(keyword)), value(std::move(val)) {}
};

// Importação de Biblioteca Externa / DLL via FFI
struct DllImportStmt : public Stmt {
    std::string dllPath;

    explicit DllImportStmt(std::string path) 
        : dllPath(std::move(path)) {}
};

struct SleepStmt : public Stmt {
    Token keyword;
    std::unique_ptr<Expr> duration;

    SleepStmt(Token keyword, std::unique_ptr<Expr> dur)
        : keyword(std::move(keyword)), duration(std::move(dur)) {}
};

#endif // AST_HPP
