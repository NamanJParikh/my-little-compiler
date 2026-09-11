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
    // Variable name
    tok_varname = -2,
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

    // Variable names or keywords - starting with a letter
    if (isalpha(LastChar)) {
        IdentifierStr = LastChar;
        // read rest of alphanumeric string
        while (isalnum((LastChar = getchar()))) {
            IdentifierStr += LastChar;
        }
        // identify if it's a keyword. if not, it's a variable name
        if (IdentifierStr == "def") {
            return tok_def;
        } else {
            return tok_varname;
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