#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

/* 
    #############################
    ###         Lexer         ###
    #############################

    For get token, the possible tokens we can read are
     - Numbers (numeric string with <= 1 '.')
     - Variable names (alphanumeric strings starting with a letter)
     - Function names (same as variable names, but followed by '(')
     - Keywords (e.g. 'def')
     - Comments (starting with '#' and possibly multi-line with '#*' and '*#')
     - Special characters (e.g. '+', '=', '(', etc.), may require further parsing
     - End of file (EOF)
*/

static std::string IdentifierStr;   // filled in with variable name
static double NumVal;               // filled in with number value

// Possible token types
// Special characters are returned as their ASCII value, comments are skipped
enum Token {
    // Number
    tok_number = -1,
    // Variable/function name
    tok_name = -2,
    // Keywords
    tok_def = -3,
    // EOF
    tok_eof = -4
};

/* 
    ##### gettok ##### 
    Reads the next token from standard input and returns its type, filling in
      IdentifierStr or NumVal if appropriate.
*/
static int gettok() {
    static int LastChar = ' ';

    // Whitespace - ignore
    while (isspace(LastChar)) {
        LastChar = getchar();
    }
    
    // Numbers
    //* this is only positive values ??, need negative sign
    //* this allows numbers like 1.2.3, needs to be fixed
    if (isdigit(LastChar) || LastChar == '.') {
        std::string NumStr;
        while (isdigit(LastChar) || LastChar == '.') {
            NumStr += LastChar;
            LastChar = getchar();
        }
        // convert the string to a double
        NumVal = strtod(NumStr.c_str(), 0);
        return tok_number;
    }

    // Variable/function names or keywords - starting with a letter
    if (isalpha(LastChar)) {
        IdentifierStr = LastChar;
        // read rest of alphanumeric string or underscore for variable/function name
        while (isalnum((LastChar = getchar())) || LastChar == '_') {
            IdentifierStr += LastChar;
        }
        // identify if it's a keyword. if not, it's a variable/function name
        if (IdentifierStr == "def") {
            return tok_def;
        } else {
            return tok_name;
        }
    }
    
    // Comments - starting with '#'
    if (LastChar == '#') {
        // is it a multiline comment?
        LastChar = getchar();
        if (LastChar == '*') {
            // skip until the multiline comment ends upon '*#'
            while (true) {
                LastChar = getchar();
                if (LastChar == '*') {
                    LastChar = getchar();
                    if (LastChar == '#') {
                        break;
                    }
                }
            }
        } else {
            // skip until the end of the line
            while (LastChar != EOF && LastChar != '\n' && LastChar != '\r') {
                LastChar = getchar();
            }
        }
        // since we skipped the comment, get the next token and return that
        return gettok();
    }

    // End of file
    if (LastChar == EOF) {
        return tok_eof;
    }

    // Special characters
    int ThisChar = LastChar;
    LastChar = getchar();
    return ThisChar;

}

/* 
    #############################
    ###          AST          ###
    #############################

    The most basic form of Kaleidescope contains two types of statements: 
    expressions and function definitions.

    Expressions are built from either numerical values or variable names. Binary 
    operators and functions are applied to build more complex expressions. This
    justifies the following classes:
     - ExprAST: base class for expressions
      - NumberExprAST: numeric values (e.g. "123.4")
      - VariableExprAST: variable names (e.g. "myVar")
      - BinaryExprAST: binary operators (e.g. "myVar + 123.4")
      - CallExprAST: function calls (e.g. "myFunc(myVar, 123.4)")
    
    Functions are defined as a prototype (def funcName(arg1, arg2, ...)) and a 
    body, which is an expression. This justifies the following classes:
     - PrototypeAST: function prototype
     - FunctionAST: function definition (prototype + body expression)
      
*/

class ExprAST  {
    public:
        virtual ~ExprAST() = default;
};

class NumberExprAST : public ExprAST {
    private:
        double Val;                                 // Numerical value
    public:
        NumberExprAST(double Val) : Val(Val) {}
};

class VariableExprAST : public ExprAST {
    private:
        std::string Name;                           // Variable name
    public:
        VariableExprAST(const std::string &Name) : Name(Name) {}
};

//* TODO- will later want to extend the character Op to a string to support
//* multi-character operators like ":=", "<=", etc.
class BinaryExprAST : public ExprAST {
    private:
        char Op;                                    // Operator
        std::unique_ptr<ExprAST> LHS, RHS;          // Expressions operator acts on
    public:
        BinaryExprAST(char Op, std::unique_ptr<ExprAST> LHS, std::unique_ptr<ExprAST> RHS)
            : Op(Op), LHS(std::move(LHS)), RHS(std::move(RHS)) {}
};

class CallExprAST : public ExprAST {
    private:
        std::string Callee;                         // Function name
        std::vector<std::unique_ptr<ExprAST>> Args; // Function inputs
    public:
        CallExprAST(const std::string &Callee, std::vector<std::unique_ptr<ExprAST>> Args)
            : Callee(Callee), Args(std::move(Args)) {}
};

class PrototypeAST {
    private:
        std::string Name;                           // Function name
        std::vector<std::string> Args;              // Function inputs
    public:
        PrototypeAST(const std::string &Name, std::vector<std::string> Args)
            : Name(Name), Args(std::move(Args)) {}

        const std::string &getName() const { return Name; }
};

class FunctionAST {
    private:
        std::unique_ptr<PrototypeAST> Proto;        // Prototype
        std::unique_ptr<ExprAST> Body;              // Body expression
    public:
        FunctionAST(std::unique_ptr<PrototypeAST> Proto,
                std::unique_ptr<ExprAST> Body)
        : Proto(std::move(Proto)), Body(std::move(Body)) {}
};