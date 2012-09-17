#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <errno.h>

#include "util.h"
#include "struct.h"
#include "serial.h"
#include "compile.h"
#include "vm.h"
#include "variable.h"

#define TAG              "compile"
#define ERROR_LEX        "Lexigraphical error"
#define ERROR_PARSE      "Parsological error"
#define ERROR_TOKEN      "Unknown token"
#define QUOTE            '\''
#define ESCAPE           '\\'
#define ESCAPED_NEWLINE  'n'
#define ESCAPED_TAB      't'
#define ESCAPED_QUOTE    '\''

uint32_t line;
struct array* lex_list;
struct map *imports = NULL;
struct byte_array *read_file(const struct byte_array *filename);

// token ///////////////////////////////////////////////////////////////////

    enum Lexeme {
        LEX_NONE,
        LEX_IMPORT,
        LEX_LEFT_COMMENT,
        LEX_RIGHT_COMMENT,
        LEX_PLUS,
        LEX_MINUS,
        LEX_TIMES,
        LEX_DIVIDE,
        LEX_MODULO,
        LEX_AND,
        LEX_OR,
        LEX_NOT,
        LEX_BAND,
        LEX_BOR,
        LEX_INVERSE,
        LEX_XOR,
        LEX_LSHIFT,
        LEX_RSHIFT,
        LEX_GREATER,
        LEX_LESSER,
        LEX_GREAQUAL,
        LEX_LEAQUAL,
        LEX_SAME,
        LEX_SET,
        LEX_DIFFERENT,
        LEX_COMMA,
        LEX_PERIOD,
        LEX_COLON,
        LEX_LINE_COMMENT,
        LEX_LEFTHESIS,
        LEX_RIGHTHESIS,
        LEX_LEFTSQUARE,
        LEX_RIGHTSQUARE,
        LEX_LEFTSTACHE,
        LEX_RIGHTSTACHE,
        LEX_IDENTIFIER,
        LEX_STRING,
        LEX_INTEGER,
        LEX_TRUE,
        LEX_FALSE,
        LEX_IF,
        LEX_THEN,
        LEX_ELSE,
        LEX_END,
        LEX_WHILE,
        LEX_FOR,
        LEX_IN,
        LEX_WHERE,
        LEX_FUNCTION,
        LEX_RETURN,
        LEX_TRY,
        LEX_CATCH,
        LEX_THROW,
        LEX_DO,
        LEX_NEG,
        LEX_NIL,
    };

struct token {
    enum Lexeme lexeme;
    uint32_t number;
    struct byte_array *string;
    uint32_t at_line;
};

struct number_string lexemes[] = {
    {LEX_IMPORT,                "import"},
    {LEX_LEFT_COMMENT,          "/*"},
    {LEX_RIGHT_COMMENT,         "*/"},
    {LEX_LINE_COMMENT,          "#"},
    {LEX_PLUS,                  "+"},
    {LEX_MINUS,                 "-"},
    {LEX_TIMES,                 "*"},
    {LEX_DIVIDE,                "/"},
    {LEX_MODULO,                "%"},
    {LEX_BAND,                  "&"},
    {LEX_BOR,                   "|"},
    {LEX_INVERSE,               "~"},
    {LEX_XOR,                   "^"},
    {LEX_RSHIFT,                ">>"},
    {LEX_LSHIFT,                "<<"},
    {LEX_AND,                   "and"},
    {LEX_OR,                    "or"},
    {LEX_NOT,                   "not"},
    {LEX_GREAQUAL,              ">="},
    {LEX_LEAQUAL,               "<="},
    {LEX_GREATER,               ">"},
    {LEX_LESSER,                "<"},
    {LEX_SAME,                  "=="},
    {LEX_SET,                   "="},
    {LEX_DIFFERENT,             "!="},
    {LEX_COMMA,                 ","},
    {LEX_PERIOD,                "."},
    {LEX_COLON,                 ":"},
    {LEX_LEFTHESIS,             "("},
    {LEX_RIGHTHESIS,            ")"},
    {LEX_LEFTSQUARE,            "["},
    {LEX_RIGHTSQUARE,           "]"},
    {LEX_LEFTSTACHE,            "{"},
    {LEX_RIGHTSTACHE,           "}"},
    {LEX_TRUE,                  "true"},
    {LEX_FALSE,                 "false"},
    {LEX_IF,                    "if"},
    {LEX_THEN,                  "then"},
    {LEX_ELSE,                  "else"},
    {LEX_END,                   "end"},
    {LEX_WHILE,                 "while"},
    {LEX_FUNCTION,              "function"},
    {LEX_FOR,                   "for"},
    {LEX_IN,                    "in"},
    {LEX_WHERE,                 "where"},
    {LEX_RETURN,                "return"},
    {LEX_TRY,                   "try"},
    {LEX_CATCH,                 "catch"},
    {LEX_THROW,                 "throw"},
    {LEX_DO,                    "do"},
    {LEX_NIL,                   "nil"},
};

const char* lexeme_to_string(enum Lexeme lexeme)
{
    switch (lexeme) {
        case LEX_IDENTIFIER:    return "id";
        case LEX_STRING:        return "string";
        case LEX_INTEGER:       return "number";
        case LEX_NEG:           return "-";
        default:
            return NUM_TO_STRING(lexemes, lexeme);
    }
}

// display ////////////////////////////////////////////////////////////////////

#ifdef DEBUG

void display_token(struct token *token, int depth) {
    assert_message(depth < 10, "kablooie!");
    char* indent = (char*)malloc(sizeof(char)*(depth+1));
    int i;
    for (i=0; i<depth; i++)
        indent[i] = '\t';
    indent[i] = 0;
    DEBUGPRINT("%s%d ", indent, token->lexeme);

    switch (token->lexeme) {
        case LEX_NONE:                                                break;
        case LEX_INTEGER:    DEBUGPRINT("%u", token->number);        break;
        case LEX_STRING:
        case LEX_IDENTIFIER: {
            char* s = byte_array_to_string(token->string);
            DEBUGPRINT("%s:%s", lexeme_to_string(token->lexeme), s);
            free(s);
        } break;
        case LEX_FUNCTION:
            DEBUGPRINT("fdecl");
            break;
        default:
            DEBUGPRINT("%s", lexeme_to_string(token->lexeme));
            break;
    }
    DEBUGPRINT("\n");
    free(indent);
}

void display_lex_list() {
    int i, n = lex_list->length;
    for (i=0; i<n; i++) {
        DEBUGPRINT("\t%d: ", i);
        display_token((struct token*)array_get(lex_list,i), 0);
    }
}

#endif

// lex /////////////////////////////////////////////////////////////////////

struct array* lex(struct byte_array *binput);

struct token *token_new(enum Lexeme lexeme, int at_line) {
    struct token *t = (struct token*)malloc(sizeof(struct token));
    t->lexeme = lexeme;
    t->string = NULL;
    t->number = 0;
    t->at_line = line;
    return t;
}

struct token *insert_token(enum Lexeme lexeme) {
    struct token *token = token_new(lexeme, line);
    array_add(lex_list, token);
    return token;
}

int insert_token_number(const char *input, int i) {
    struct token *token = insert_token(LEX_INTEGER);
    token->number = atoi(&input[i]);
    //while (isdigit(input[i++]));
    for (; isdigit(input[i]); i++);
    return i;
}

bool isiden(char c)
{
    return isalnum(c) || (c=='_');
}

bool isKeyword(int n)
{
    for (int c,i=0; (c = lexemes[n].chars[i]); i++)
        if (!isalpha(c))
            return false;
    return true;
}

int insert_token_string(enum Lexeme lexeme, const char* input, int i)
{
    struct byte_array *string = byte_array_new();
    while ((lexeme==LEX_IDENTIFIER && isiden(input[i])) ||
           (lexeme==LEX_STRING && input[i] != QUOTE)) {
        uint8_t c = input[i++];
        //DEBUGPRINT("@%d c=%3d %c\n", i, c, c);
        if (c == ESCAPE) {
            c = input[i++];
            switch (c) {
                case ESCAPED_NEWLINE:    c = '\n';                        break;
                case ESCAPED_TAB:        c = '\t';                        break;
                case ESCAPED_QUOTE:      c = '\'';                        break;
                default:
                    return (VOID_INT)exit_message("unknown escape");
            }
        }
        byte_array_add_byte(string, c);
    }

    int end = i + (lexeme==LEX_STRING);
    struct token *token = token_new(lexeme, line);
    token->string = string;
    array_add(lex_list, token);
    //display_token(token, 0);
    return end;
}

int insert_lexeme(int index) {
    struct number_string sn = lexemes[index];
    insert_token((enum Lexeme)sn.number);
    return strlen(sn.chars);
}

int import(const char* input, int i)
{
    i += strlen(lexeme_to_string(LEX_IMPORT));
    while (isspace(input[i++]));
    struct byte_array *path = byte_array_new();
    while (!isspace(input[i]) && input[i]!=QUOTE)
        byte_array_add_byte(path, input[i++]);
    byte_array_append(path, byte_array_from_string(EXTENSION_SRC));

    if (!map_has(imports, path)) {
        map_insert(imports, path, NULL);
        struct byte_array *imported = read_file(path);
        lex(imported);
    }

    return i+1;
}

struct array* lex(struct byte_array *binput)
{
    int i=0,j;
    char c;
    line = 1;

    const char* input = byte_array_to_string(binput);
    const char* right_comment = lexeme_to_string(LEX_RIGHT_COMMENT);
    int right_comment_len = strlen(right_comment);

lexmore:
    while (i < binput->length) {

        for (j=0; j<ARRAY_LEN(lexemes); j++) {

            char* lexstr = lexemes[j].chars;

            if (!strncmp(&input[i], lexstr, strlen(lexstr))) {

                if (isKeyword(j) && i<binput->length-1 && isalpha(input[i+strlen(lexemes[j].chars)])) {
                    //DEBUGPRINT("%s is a keyword, %c is alpha\n", lexemes[j].chars, input[i+strlen(lexemes[j].chars)]);
                    continue; // just an identifier
                }
                enum Lexeme lexeme = (enum Lexeme)lexemes[j].number;

                if (lexeme == LEX_LEFT_COMMENT) { // start comment with /*
                    while (strncmp(&input[i], right_comment, right_comment_len))
                        if (input[i++] == '\n')
                            line++;
                    i += right_comment_len;
                } else if (lexeme == LEX_LINE_COMMENT) { // start line comment with #
                    while (input[i++] != '\n');
                    line++;
                } else if (lexeme == LEX_IMPORT)
                    i = import(input, i);
                else {
                    //DEBUGPRINT("lexeme=%d\n",lexeme);
                    i += insert_lexeme(j);
                }
                goto lexmore;
            }
        }

        c = input[i];

        if (isdigit(c))         i = insert_token_number(input,i);
        else if (isiden(c))     i = insert_token_string(LEX_IDENTIFIER, input, i);
        else if (c == '\n')     { line++; i++; }
        else if (isspace(c))    i++;
        else if (c=='\'')       i = insert_token_string(LEX_STRING, input, i+1);
        else
            return (struct array*)exit_message(ERROR_LEX);
    }
#ifdef DEBUG
//    display_lex_list();
#endif
    return lex_list;
}

/* parse ///////////////////////////////////////////////////////////////////

BNF:

<statements> --> ( <assignment> | <fcall> | <ifthenelse> | <loop> | <rejoinder> | <iterloop> | <trycatch> | <throw> )*
<trycatch> --> LEX_TRY <statements> LEX_CATCH <variable> <statements> LEX_END
<throw> --> LEX_THROW <expression>
<assignment> --> <destination>, LEX_SET <source>,
<destination> --> <variable> | ( <expression> <member> )
<source> --> <expression>

<ifthenelse> --> IF <expression> THEN <statements>
                  ( ELSE IF <expression> THEN <statements> )*
                  ( ELSE <statements>)? LEX_END
<loop> --> WHILE <expression> LEX_DO <statements> LEX_END

<iterator> --> LEX_FOR LEX_IDENTIFIER LEX_IN <expression> ( LEX_WHERE <expression> )?
<iterloop> --> <iterator> LEX_DO <statements> LEX_END
<comprehension> --> LEX_LEFTSQUARE <expression> <iterator> LEX_RIGHTSQUARE

<rejoinder> --> LEX_RETURN <source>,
<paramdecl> --> LEX_LEFTHESIS <variable>, LEX_RIGHTHESIS
<fdecl> --> FUNCTION <paramdecl> (<paramdecl>) <statements> LEX_END
<fcall> --> <expression> <call>
<call> --> LEX_LEFTHESIS <source>, LEX_RIGHTHESIS

<expression> --> <exp2> ( ( LEX_SAME | LEX_DIFFERENT | LEX_GREATER | LEX_GREAQUAL | LEX_LEAQUAL | LEX_LESSER ) <exp2> )?
<exp2> --> <exp3> ( ( LEX_PLUS | LEX_MINUS ) <exp3> )*
<exp3> --> <exp4> ( ( LEX_TIMES | LEX_DIVIDE | LEX_MODULO | LEX_AND | LEX_OR
                      LEX_BAND | LEX_BOR | LEX_XOR | LEX_LSHIFT | LEX_RSHIFT) <exp3> )*
<exp4> --> (LEX_NOT | LEX_MINUS | LEX_INVERSE)? <exp5>
<exp5> --> <exp6> ( <call> | <member> )*
<exp6> --> ( LEX_LEFTTHESIS <expression> LEX_RIGHTTHESIS ) | <atom>
<atom> -->  LEX_IDENTIFIER | <float> | <integer> | <boolean> | <nil> | <table> | <comprehension> | <fdecl>

<variable> --> LEX_IDENTIFIER
<boolean> --> LEX_TRUE | LEX_FALSE
<floater> --> LEX_INTEGER LEX_PERIOD LEX_INTEGER
<string> --> LEX_STRING
<table> --> LEX_LEFTSQUARE <element>, LEX_RIGHTSQUARE
<element> --> <expression> ( LEX_COLON <expression> )?
<member> --> ( LEX_LEFTSQUARE <expression> LEX_RIGHTSQUARE ) | ( LEX_PERIOD LEX_STRING )

*///////////////////////////////////////////////////////////////////////////

    enum Nonterminal {
        SYMBOL_STATEMENTS,
        SYMBOL_ASSIGNMENT,
        SYMBOL_SOURCE,
        SYMBOL_DESTINATION,
        SYMBOL_IF_THEN_ELSE,
        SYMBOL_LOOP,
        SYMBOL_EXPRESSION,
        SYMBOL_INTEGER,
        SYMBOL_FLOAT,
        SYMBOL_STRING,
        SYMBOL_VARIABLE,
        SYMBOL_TABLE,
        SYMBOL_PAIR,
        SYMBOL_FDECL,
        SYMBOL_FCALL,
        SYMBOL_MEMBER,
        SYMBOL_RETURN,
        SYMBOL_BOOLEAN,
        SYMBOL_NIL,
        SYMBOL_ITERATOR,
        SYMBOL_ITERLOOP,
        SYMBOL_COMPREHENSION,
        SYMBOL_TRYCATCH,
        SYMBOL_THROW,
    } nonterminal;

struct symbol {
    enum Nonterminal nonterminal;
    struct token *token;
    struct array* list;
    struct symbol *index, *value, *other;
    float floater;
    bool lhs;
};

struct number_string nonterminals[] = {
    {SYMBOL_STATEMENTS,     "statements"},
    {SYMBOL_ASSIGNMENT,     "assignment"},
    {SYMBOL_SOURCE,         "source"},
    {SYMBOL_DESTINATION,    "destination"},
    {SYMBOL_IF_THEN_ELSE,   "if-then-else"},
    {SYMBOL_LOOP,           "loop"},
    {SYMBOL_EXPRESSION,     "expression"},
    {SYMBOL_INTEGER,        "integer"},
    {SYMBOL_FLOAT,          "float"},
    {SYMBOL_STRING,         "string"},
    {SYMBOL_VARIABLE,       "variable"},
    {SYMBOL_TABLE,          "table"},
    {SYMBOL_PAIR,           "pair"},
    {SYMBOL_FDECL,          "fdecl"},
    {SYMBOL_FCALL,          "fcall"},
    {SYMBOL_MEMBER,         "member"},
    {SYMBOL_RETURN,         "return"},
    {SYMBOL_BOOLEAN,        "boolean"},
    {SYMBOL_NIL,            "nil"},
    {SYMBOL_ITERATOR,       "iterator"},
    {SYMBOL_ITERLOOP,       "iterloop"},
    {SYMBOL_COMPREHENSION,  "comprehension"},
    {SYMBOL_TRYCATCH,       "try-catch"},
    {SYMBOL_THROW,          "throw"},
};

struct array* parse_list;
uint32_t parse_index;
struct symbol *expression();
struct symbol *statements();
struct symbol *comprehension();

struct symbol *symbol_new(enum Nonterminal nonterminal)
{
    //    DEBUGPRINT("symbol_new %s\n", NUM_TO_STRING(nonterminals, ARRAY_LEN(nonterminals), nonterminal));
    struct symbol *s = (struct symbol*)malloc(sizeof(struct symbol));
    s->nonterminal = nonterminal;
    s->list = array_new();
    s->index = s->value = s->other = NULL;
    s->lhs = false;
    return s;
}

struct symbol *symbol_add(struct symbol *s, struct symbol *t)
{
    null_check(s);
    if (!t)
        return NULL;
    //DEBUGPRINT("symbol_add %s\n", nonterminals[t->nonterminal]);
    array_add(s->list, t);
    return s;
}

#ifdef DEBUG

void display_symbol(const struct symbol *symbol, int depth)
{
    //    null_check(symbol);
    if (!symbol)
        return;
    assert_message(depth < 100, "kablooie!");
    char* indent = (char*)malloc(sizeof(char)*(depth+1));
    int i;
    for (i=0; i<depth; i++)
        indent[i] = '\t';
    indent[i] = 0;
    DEBUGPRINT("%s%d %s", indent, symbol->nonterminal, NUM_TO_STRING(nonterminals, symbol->nonterminal));

    switch (symbol->nonterminal) {
        case SYMBOL_INTEGER:
            DEBUGPRINT(": %u\n", symbol->token->number);
            break;
        case SYMBOL_FLOAT:
            DEBUGPRINT(": %f\n", symbol->floater);
            break;
        case SYMBOL_VARIABLE: {
            char* s = byte_array_to_string(symbol->token->string);
            DEBUGPRINT(": '%s (%s)'\n", s, symbol->lhs ? "lhs":"rhs");
            free(s);
        } break;
        case SYMBOL_STRING: {
            char* s = byte_array_to_string(symbol->token->string);
            DEBUGPRINT(": '%s'\n", s);
            free(s);
        } break;
        case SYMBOL_EXPRESSION:
            DEBUGPRINT(" %s\n", lexeme_to_string(symbol->token->lexeme));
            break;
        case SYMBOL_TRYCATCH:
            DEBUGPRINT("try\n");
            display_symbol(symbol->index, depth);
            DEBUGPRINT("catch %s\n", byte_array_to_string(symbol->token->string));
            display_symbol(symbol->value, depth);
            break;
        default:
            DEBUGPRINT("\n");
            depth++;
            display_symbol(symbol->index, depth);
            display_symbol(symbol->value, depth);
            depth--;
            break;
    }

    const struct array *list = symbol->list;
    if (list && list->length) {
        DEBUGPRINT("%s\tlist:\n", indent);
        depth++;
        for (int k=0; k<list->length; k++) {
            const struct symbol *item = array_get(list, k);
            display_symbol(item, depth);
        }
    }

    free(indent);
}

#endif

#define LOOKAHEAD (lookahead(0))
#define FETCH_OR_QUIT(x) if (!fetch(x)) return NULL;
#define OR_ERROR(x) { exit_message("missing %s at line %d", NUM_TO_STRING(lexemes, x), line); return NULL; }
#define FETCH_OR_ERROR(x) if (!fetch(x)) OR_ERROR(x);


enum Lexeme lookahead(int n) {
    if (parse_index + n >= parse_list->length)
        return LEX_NONE;
    struct token *token = (struct token*)parse_list->data[parse_index+n];
    assert_message(token!=0, ERROR_NULL);
    return token->lexeme;
}

struct token *fetch(enum Lexeme lexeme) {
    if (parse_index >= parse_list->length)
        return NULL;
    struct token *token = (struct token*)parse_list->data[parse_index];
    if (token->lexeme != lexeme) {
        //DEBUGPRINT("fetched %s instead of %s at %d\n", lexeme_to_string(token->lexeme), lexeme_to_string(lexeme), parse_index);
        return NULL;
    }
    DEBUGPRINT("fetched %s at %d\n", lexeme_to_string(lexeme), parse_index);
    // display_token(token, 0);

    parse_index++;
    return token;
}

typedef struct symbol*(Parsnip)();

struct symbol *one_of(Parsnip *p, ...) {
    uint32_t start = parse_index;
    struct symbol *t = NULL;
    va_list argp;
    va_start(argp, p);
    for (;
         p && !(t=p());
         p = va_arg(argp, Parsnip*))
        parse_index=start;
    va_end(argp);
    return t;
}

struct token *fetch_lookahead(enum Lexeme lexeme, ...) {
    struct token *t=NULL;
    va_list argp;
    va_start(argp, lexeme);

    for (; lexeme; lexeme = (enum Lexeme)va_arg(argp, int)) {
        if (LOOKAHEAD == lexeme) {
            t = fetch(lexeme);
            break;
        }
    }
    va_end(argp);
    return t;
}

struct symbol *symbol_fetch(enum Nonterminal n, enum Lexeme goal, ...)
{
    if (parse_index >= parse_list->length)
        return NULL;
    struct token *token = (struct token*)parse_list->data[parse_index];
    assert_message(token!=0, ERROR_NULL);
    enum Lexeme lexeme = token->lexeme;

    struct symbol *symbol = NULL;

    va_list argp;
    va_start(argp, goal);

    for (; goal; goal = (enum Lexeme)va_arg(argp, int)) {
        if (lexeme == goal) {

            symbol = symbol_new(n);
            symbol->token = token;
            DEBUGPRINT("fetched %s at %d\n", lexeme_to_string(lexeme), parse_index);
            // display_token(token, 0);

            parse_index++;
            break;
        }
    }
    va_end(argp);
    return symbol;
}

struct symbol *symbol_adds(struct symbol *s, struct symbol *child, ...) {
    va_list argp;
    va_start(argp, child);
    for (; child; child = va_arg(argp, struct symbol*))
        array_add(s->list, child);
    va_end(argp);
    return s;

}

// <x>, --> ( <x> ( LEX_COMMA <x> )* )?
// e.g. a list of zero or more <x>, separated by commas
struct symbol *repeated(enum Nonterminal nonterminal, Parsnip *p)
{
    struct symbol *r, *s = symbol_new(nonterminal);
    do {
        if (!(r=p()))
            break;
        symbol_add(s, r);
    } while (fetch(LEX_COMMA));
    return s;
}

//////////////////////////////// BNFs


// <destination> --> <variable> | ( <expression> <member> )
struct symbol *destination()
{
    struct symbol *e = expression();
    if (!e ||
        (e->nonterminal != SYMBOL_MEMBER &&
         e->nonterminal != SYMBOL_VARIABLE))
        return NULL;
    e->lhs = true;
    return e;
};

// <variable> --> LEX_IDENTIFIER
struct symbol *variable()
{
    return symbol_fetch(SYMBOL_VARIABLE, LEX_IDENTIFIER, NULL);
}

// <paramdecl> --> LEX_LEFTHESIS <variable>, LEX_RIGHTHESIS
struct symbol *paramdecl()
{
    FETCH_OR_QUIT(LEX_LEFTHESIS);
    struct symbol *s = repeated(SYMBOL_VARIABLE, &variable);
    FETCH_OR_ERROR(LEX_RIGHTHESIS)
    return s;
}

// <fdecl> --> FUNCTION <paramdecl> ( <paramdecl> ) <statements> LEX_END
struct symbol *fdecl()
{
    FETCH_OR_QUIT(LEX_FUNCTION)
    struct symbol *s = symbol_new(SYMBOL_FDECL);

    FETCH_OR_ERROR(LEX_LEFTHESIS);
    s->index = repeated(SYMBOL_DESTINATION, &destination);
    FETCH_OR_ERROR(LEX_RIGHTHESIS)

    if (fetch_lookahead(LEX_LEFTHESIS)) {
        s->other = repeated(SYMBOL_VARIABLE, &variable);
        FETCH_OR_ERROR(LEX_RIGHTHESIS)
    }

    s->value = statements();
    FETCH_OR_ERROR(LEX_END);
    return s;
}

// <element> --> <expression> ( LEX_COLON <expression> )?
struct symbol *element()
{
    struct symbol *e = expression();
    if (fetch(LEX_COLON)) { // i.e. x:y
        struct symbol *p = symbol_new(SYMBOL_PAIR);
        p->index = e;
        p->value = expression();
        return p;
    } else {
        return e;
    }
}

// <table> --> LEX_LEFTSQUARE <element>, LEX_RIGHTSQUARE
struct symbol *table() {
    FETCH_OR_QUIT(LEX_LEFTSQUARE);
    struct symbol *s = repeated(SYMBOL_TABLE, &element);
    FETCH_OR_ERROR(LEX_RIGHTSQUARE);
    return s;
}

struct symbol *integer()
{
    struct token *t = fetch(LEX_INTEGER);
    if (!t)
        return NULL;
    struct symbol *s = symbol_new(SYMBOL_INTEGER);
    s->token = t;
    return s;
}

struct symbol *boolean()
{
    struct token *t = fetch_lookahead(LEX_TRUE, LEX_FALSE, NULL);
    if (!t)
        return NULL;
    struct symbol *s = symbol_new(SYMBOL_BOOLEAN);
    s->token = t;
    return s;
}

struct symbol *nil()
{
    struct token *t = fetch(LEX_NIL);
    if (!t)
        return NULL;
    struct symbol *s = symbol_new(SYMBOL_NIL);
    s->token = t;
    return s;
}

struct symbol *floater()
{
    struct token *t = fetch(LEX_INTEGER);
    if (!t)
        return NULL;
    FETCH_OR_QUIT(LEX_PERIOD);
    struct token *u = fetch(LEX_INTEGER);

    float decimal = u->number;
    while (decimal > 1)
        decimal /= 10;

    struct symbol *s = symbol_new(SYMBOL_FLOAT);
    s->floater = t->number + decimal;
    return s;
}

// <string> --> LEX_STRING
struct symbol *string()
{
    struct token *t = fetch(LEX_STRING);
    if (!t)
        return NULL;
    struct symbol *s = symbol_new(SYMBOL_STRING);
    s->token = t;
    return s;
}

//  <atom> -->  LEX_IDENTIFIER | <float> | <integer> | <boolean> | <nil> | <table> | <comprehension> | <fdecl>
struct symbol *atom()
{
    return one_of(&variable, &string, &floater, &integer, &boolean, &nil, &comprehension, &table, &fdecl, NULL);
}

// <exp5> --> ( LEX_LEFTTHESIS <expression> LEX_RIGHTTHESIS ) | <atom>
struct symbol *exp5()
{
    if (fetch(LEX_LEFTHESIS)) {
        struct symbol *s = expression();
        fetch(LEX_RIGHTHESIS);
        return s;
    }
    return atom();
}

// <member> --> ( LEX_LEFTSQUARE <expression> LEX_RIGHTSQUARE ) | ( LEX_PERIOD LEX_STRING )
struct symbol *member()
{
    struct symbol *m = symbol_new(SYMBOL_MEMBER);

    if (fetch(LEX_PERIOD)) {
        m->index = variable();
        m->index->nonterminal = SYMBOL_STRING;
    }
    else {
        FETCH_OR_QUIT(LEX_LEFTSQUARE);
        m->index = expression();
        FETCH_OR_QUIT(LEX_RIGHTSQUARE);
    }
    return m;
}

// <call> --> LEX_LEFTHESIS <source>, LEX_RIGHTHESIS
struct symbol *call()
{
//    struct symbol *s = symbol_new(SYMBOL_FCALL);
    FETCH_OR_QUIT(LEX_LEFTHESIS);
//    s->index = repeated(SYMBOL_FCALL, &expression); // arguments
    struct symbol *s = repeated(SYMBOL_FCALL, &expression); // arguments
    FETCH_OR_ERROR(LEX_RIGHTHESIS);
    return s;
}

// <fcall> --> <expression> <call>
struct symbol *fcall()
{
    struct symbol *e = expression();
    return (e && e->nonterminal ==  SYMBOL_FCALL) ? e : NULL;
}

// <exp4> --> <exp5> ( <call> | member )*
struct symbol *exp4()
{
    struct symbol *g, *f;
    f = exp5();
    while (f && (g = one_of(&call, &member, NULL))) {
        g->value = f;
        f = g;
    }
    return f;
}

// <exp3> --> (NOT | LEX_MINUS)? <exp4>
struct symbol *exp3()
{
    struct symbol *e;
    if ((e = symbol_fetch(SYMBOL_EXPRESSION, LEX_MINUS, LEX_NEG, LEX_NOT, NULL))) {
        if (e->token->lexeme == LEX_MINUS)
            e->token->lexeme = LEX_NEG;
        return symbol_add(e, exp4());
    }
    return exp4();
}

// <exp2> --> (<exp3> ( ( LEX_PLUS | LEX_MINUS | LEX_TIMES | LEX_DIVIDE | LEX_MODULO ))* <exp3>
struct symbol *expTwo()
{
    struct symbol *e, *f;
    e = exp3();
    while (e && (f = symbol_fetch(SYMBOL_EXPRESSION, LEX_PLUS, LEX_MINUS, LEX_TIMES, LEX_DIVIDE, LEX_MODULO, LEX_OR, LEX_AND, NULL)))
        e = symbol_adds(f, e, exp3(), NULL);
    return e;
}

// <expression> --> <exp2> ( ( LEX_SAME | LEX_DIFFERENT | LEX_GREATER | LEX_GREAQAL | LEX_LEQUAL | LEX_LESSER ) <exp2> )?
struct symbol *expression()
{
    struct symbol *f, *e = expTwo();
    while ((f = symbol_fetch(SYMBOL_EXPRESSION, LEX_SAME, LEX_DIFFERENT, LEX_GREATER, LEX_LESSER, LEX_GREAQUAL, LEX_LEAQUAL, NULL)))
        e = symbol_adds(f, e, expTwo(), NULL);
    return e;
}

// <assignment> --> <destination>, LEX_SET <source>,
struct symbol *assignment()
{
    struct symbol *d = repeated(SYMBOL_DESTINATION, &destination);
    FETCH_OR_QUIT(LEX_SET);
    struct symbol *s = symbol_new(SYMBOL_ASSIGNMENT);
    s->value = repeated(SYMBOL_SOURCE, expression);
    s->index = d;
    return s;
}

/*    <ifthenelse> --> IF <expression>
                        THEN <statements>
                        (ELSE IF <expression> THEN <statements>)*
                        (ELSE <statements>)?
                     END */
struct symbol *ifthenelse()
{
    FETCH_OR_QUIT(LEX_IF);
    struct symbol *f = symbol_new(SYMBOL_IF_THEN_ELSE);
    symbol_add(f, expression());
    fetch(LEX_THEN);
    symbol_add(f, statements());

    while (lookahead(0) == LEX_ELSE && lookahead(1) == LEX_IF) {
        fetch(LEX_ELSE);
        fetch(LEX_IF);
        symbol_add(f, expression());
        fetch(LEX_THEN);
        symbol_add(f, statements());
    }

    if (fetch_lookahead(LEX_ELSE, NULL))
        symbol_add(f, statements());
    fetch(LEX_END);
    return f;
}

// <loop> --> WHILE <expression> <statements> END
struct symbol *loop()
{
    FETCH_OR_QUIT(LEX_WHILE);
    struct symbol *s = symbol_new(SYMBOL_LOOP);
    s->index = expression();
    s->value = statements();
    FETCH_OR_ERROR(LEX_END);
    return s;
}

// <iterator> --> LEX_FOR LEX_IDENTIFIER LEX_IN <expression> ( LEX_WHERE <expression> )?
struct symbol *iterator()
{
    FETCH_OR_QUIT(LEX_FOR);
    struct token *t = fetch(LEX_IDENTIFIER);
    struct symbol *s = symbol_new(SYMBOL_ITERATOR);
    s->token = t;

    FETCH_OR_ERROR(LEX_IN);
    s->value = expression();
    if (fetch_lookahead(LEX_WHERE, NULL))
        s->index = expression();
    return s;
}

// <comprehension> --> LEX_LEFTSQUARE <expression> <iterator> LEX_RIGHTSQUARE
struct symbol *comprehension()
{
    //    DEBUGPRINT("comprehension\n");
    FETCH_OR_QUIT(LEX_LEFTSQUARE);
    struct symbol *s = symbol_new(SYMBOL_COMPREHENSION);
    s->value = expression();
    s->index = iterator();
    if (!s->index)
        return NULL;
    FETCH_OR_ERROR(LEX_RIGHTSQUARE);
    return s;
}

// <iterloop> --> <iterator> <statements> LEX_END
struct symbol *iterloop()
{
    struct symbol *i = iterator();
    if (!i)
        return  NULL;
    struct symbol *s = symbol_new(SYMBOL_ITERLOOP);
    s->index = i;
    s->value = statements();
    FETCH_OR_ERROR(LEX_END);
    return s;
}

// <rejoinder> --> // LEX_RETURN <expression>
struct symbol *rejoinder()
{
    FETCH_OR_QUIT(LEX_RETURN);
    return repeated(SYMBOL_RETURN, &expression); // return values
}

// <trycatch> --> LEX_TRY <statements> LEX_CATCH <destination> <statements> LEX_END
struct symbol *trycatch()
{
    FETCH_OR_QUIT(LEX_TRY);
    struct symbol *s = symbol_new(SYMBOL_TRYCATCH);
    s->index = statements();
    FETCH_OR_ERROR(LEX_CATCH);
    if (!(s->token = fetch(LEX_IDENTIFIER)))
        OR_ERROR(LEX_IDENTIFIER);
    s->lhs = true;
    s->value = statements();
    FETCH_OR_ERROR(LEX_END);
    return s;
}

// <throw> --> LEX_THROW <expression>
struct symbol *thrower()
{
    FETCH_OR_QUIT(LEX_THROW);
    struct symbol *s = symbol_new(SYMBOL_THROW);
    s->value = expression();
    return s;
}

struct symbol *strings_and_variables()
{
    struct array *sav = array_new();
    while ((array_add(sav, variable()) || array_add(sav, string())));
    return (struct symbol*)sav;
}

// <statements> --> ( <assignment> | <fcall> | <ifthenelse> | <loop> | <rejoinder> | <iterloop> ) *
struct symbol *statements()
{
    struct symbol *s = symbol_new(SYMBOL_STATEMENTS);
    struct symbol *t;
    while ((t = one_of(&assignment, &fcall, &ifthenelse, &loop, &rejoinder, &iterloop, &trycatch, &thrower, NULL)))
        symbol_add(s, t);
    return s;
}

struct symbol *parse(struct array *list, uint32_t index)
{
    DEBUGPRINT("parse:\n");
    assert_message(list!=0, ERROR_NULL);
    assert_message(index<list->length, ERROR_INDEX);

    parse_list = list;
    parse_index = index;

    struct symbol *p = statements();
#ifdef DEBUG
    display_symbol(p, 1);
#endif
    return p;
}


// generate ////////////////////////////////////////////////////////////////

struct byte_array *generate_code(struct byte_array *code, struct symbol *root);

void generate_step(struct byte_array *code, int count, int action,...)
{
    byte_array_add_byte(code, action);

    va_list argp;
    uint8_t parameter;
    for(va_start(argp, action); --count;) {
        parameter = va_arg(argp, int);
        byte_array_add_byte(code, (uint8_t)parameter);
    }
    va_end(argp);
}

void generate_items(struct byte_array *code, const struct symbol* root, bool reverse)
{
    if (!root)
        return;
    const struct array *items = root->list;
    uint32_t num_items = items->length;

    if (reverse) {
        for (int i=num_items-1; i>=0; i--) {
            struct symbol *item = (struct symbol*)array_get(items, i);
            generate_code(code, item);
        }
    } else {
        for (int i=0; i<num_items; i++) {
            struct symbol *item = (struct symbol*)array_get(items, i);
            generate_code(code, item);
        }
    }
}

void generate_items_then_op(struct byte_array *code, enum Opcode opcode, const struct symbol* root)
{
    generate_items(code, root, false);
    generate_step(code, 1, opcode);
    serial_encode_int(code, 0, root ? root->list->length : 0);
}

void generate_statements(struct byte_array *code, struct symbol *root) {
    if (root)
        generate_items(code, root, false);
}

void generate_return(struct byte_array *code, struct symbol *root) {
    generate_items_then_op(code, VM_RET, root);
}

void generate_nil(struct byte_array *code, struct symbol *root) {
    generate_step(code, 1, VM_NIL);
}

static void generate_jump(struct byte_array *code, int32_t offset) {
    if (!offset)
        return;
    generate_step(code, 1, VM_JMP);
    serial_encode_int(code, 0, offset);
}

void generate_list(struct byte_array *code, struct symbol *root) {
    generate_items_then_op(code, VM_LST, root);
}

void generate_float(struct byte_array *code, struct symbol *root)
{
    generate_step(code, 1, VM_FLT);
    serial_encode_float(code, 0, root->floater);
}

void generate_integer(struct byte_array *code, struct symbol *root) {
    generate_step(code, 1, VM_INT);
    serial_encode_int(code, 0, root->token->number);
}

void generate_string(struct byte_array *code, struct symbol *root) {
    generate_step(code, 1, VM_STR);
    serial_encode_string(code, 0, root->token->string);
}

void generate_source(struct byte_array *code, struct symbol *root) {
    generate_items_then_op(code, VM_SRC, root);
//    generate_items(code, root, true);
}

void generate_destination(struct byte_array *code, struct symbol *root)
{
    generate_items(code, root, false);
//    generate_items_then_op(code, VM_DST, root);
    generate_step(code, 1, VM_DST);
}

void generate_assignment(struct byte_array *code, struct symbol *root)
{
    generate_code(code, root->value);
    generate_code(code, root->index);
}

void generate_variable(struct byte_array *code, struct symbol *root)
{
    generate_step(code, 1, root->lhs ? VM_SET : VM_VAR);
    serial_encode_string(code, 0, root->token->string);
}

void generate_boolean(struct byte_array *code, struct symbol *root) {
    uint32_t value = 0;
    switch (root->token->lexeme) {
        case        LEX_TRUE:  value = 1;               break;
        case        LEX_FALSE: value = 0;               break;
        default:    exit_message("bad boolean value");    return;
    }
    generate_step(code, 1, VM_BUL);
    serial_encode_int(code, 0, value);
}

void generate_fdecl(struct byte_array *code, struct symbol *root)
{
    generate_step(code, 1, VM_FNC);

    if (root->other) {
        struct array *closure = root->other->list;
        serial_encode_int(code, 0, closure->length);
        for (int i=0; i<closure->length; i++) {
            struct symbol *name = (struct symbol*)array_get(closure, i);
            serial_encode_string(code, 0, name->token->string);
        }
    }
    else
        serial_encode_int(code, 0, 0);

    struct byte_array *f = byte_array_new();
    generate_code(f, root->index); // params
    generate_code(f, root->value); // statements
    serial_encode_string(code, 0, f);
}

void generate_pair(struct byte_array *code, struct symbol *root)
{
    generate_code(code, root->index);
    generate_code(code, root->value);
    generate_step(code, 2, VM_MAP, 1);
}

void generate_member(struct byte_array *code, struct symbol *root)
{
    generate_code(code, root->index);
    generate_code(code, root->value);

    enum Opcode op = root->lhs ? VM_PUT : VM_GET;
    generate_step(code, 1, op);
//    serial_encode_int(code, 0, root->list->length);
}

void generate_fcall(struct byte_array *code, struct symbol *root)
{

    if (root->value->nonterminal == SYMBOL_MEMBER) {
        generate_items(code, root, false); // arguments
//        generate_code(code, root->value); // member
        generate_code(code, root->value->index);
        generate_code(code, root->value->value);
        generate_step(code, 1, VM_MET);
    } else {
        generate_items(code, root, false); // arguments
        generate_code(code, root->value); // function
        generate_step(code, 1, VM_CAL);
    }
    serial_encode_int(code, 0, root->list->length);
}

void generate_math(struct byte_array *code, struct symbol *root)
{
    enum Lexeme lexeme = root->token->lexeme;
    generate_statements(code, root);
    enum Opcode op = VM_NIL;
    switch (lexeme) {
        case LEX_PLUS:      op = VM_ADD;    break;
        case LEX_MINUS:     op = VM_SUB;    break;
        case LEX_TIMES:     op = VM_MUL;    break;
        case LEX_DIVIDE:    op = VM_DIV;    break;
        case LEX_MODULO:    op = VM_MOD;    break;
        case LEX_AND:       op = VM_AND;    break;
        case LEX_OR:        op = VM_ORR;    break;
        case LEX_NOT:       op = VM_NOT;    break;
        case LEX_NEG:       op = VM_NEG;    break;
        case LEX_SAME:      op = VM_EQU;    break;
        case LEX_DIFFERENT: op = VM_NEQ;    break;
        case LEX_GREATER:   op = VM_GTN;    break;
        case LEX_LESSER:    op = VM_LTN;    break;
        case LEX_GREAQUAL:  op = VM_GRQ;    break;
        case LEX_LEAQUAL:   op = VM_LEQ;    break;
        default: exit_message("bad math lexeme");   break;
    }
    generate_step(code, 1, op);
}

// if A then ( B + jmp back )
void generate_loop(struct byte_array *code, struct symbol *root)
{
    struct byte_array *ifa, *b, *jmp;

    int32_t loop_length, jmp_len = 2;
    do {
        ifa = byte_array_new();
        b = byte_array_new();
        jmp = byte_array_new();

        generate_code(b, root->value);

        generate_code(ifa, root->index);
        generate_step(ifa, 1, VM_IFF);
        serial_encode_int(ifa, 0, b->length + jmp_len);

        loop_length = ifa->length + b->length;
        generate_jump(jmp, -loop_length);

    } while (jmp_len++ < jmp->length);

    struct byte_array *while_a_do_b = byte_array_concatenate(3, ifa, b, jmp);
    byte_array_append(code, while_a_do_b);
}

void generate_ifthenelse(struct byte_array *code, struct symbol *root)
{
    struct array *ifs = array_new();
    struct array *thens = array_new();
    struct byte_array *else_code = byte_array_new();

    for (int i=0; i<root->list->length; i+=2) {

        // if
        struct byte_array *if_code = byte_array_new();
        struct symbol *iff = (struct symbol*)array_get(root->list, i);
        generate_code(if_code, iff);
        // generate_step(clause, 1, VM_IFF);
        array_add(ifs, (void*)if_code);

        // then
        struct byte_array *then_code = byte_array_new();
        struct symbol *thn = (struct symbol*)array_get(root->list, i+1);
        assert_message(thn->nonterminal == SYMBOL_STATEMENTS, "branch syntax error");
        generate_code(then_code, thn);
        array_add(thens, (void*)then_code);

        // else
        if (root->list->length > i+2) {
            struct symbol *els = (struct symbol*)array_get(root->list, i+2);
            if (els->nonterminal == SYMBOL_STATEMENTS) {
                assert_message(root->list->length == i+3, "else should be the last branch");
                generate_code(else_code, els);
                break;
            }
        }
    }

    // go back and fill in where to jump to the end of if-then-elseif-else when done
    struct byte_array *combined = else_code;
    for (int j=ifs->length-1; j>=0; j--) {
        struct byte_array *then_code = (struct byte_array*)array_get(thens, j);
        generate_jump(then_code, combined->length);

        struct byte_array *if_code = (struct byte_array*)array_get(ifs, j);
        generate_step(if_code, 1, VM_IFF);
        serial_encode_int(if_code, 0, then_code->length);

        combined = byte_array_concatenate(3, if_code, then_code, combined);
    }
    byte_array_append(code, combined);
}

// <iterator> --> LEX_FOR LEX_IDENTIFIER LEX_IN <expression> ( LEX_WHERE <expression> )?
void generate_iterator(struct byte_array *code, struct symbol *root, enum Opcode op)
{
    struct symbol *ator = root->index;
    generate_code(code, ator->value);                   // IN b
    generate_step(code, 1, op);                         // iterator or comprehension
    serial_encode_string(code, 0, ator->token->string); // FOR a

    if (ator->index) {                                  // WHERE c
        struct byte_array *where = byte_array_new();
        generate_code(where, ator->index);
        serial_encode_string(code, 0, where);
    }
    else
        generate_nil(code, NULL);

    struct byte_array *what = byte_array_new();
    generate_code(what, root->value);
    serial_encode_string(code, 0, what);    // DO d
}

// <iterloop> --> <iterator> <statements> LEX_END
void generate_iterloop(struct byte_array *code, struct symbol *root) {
    generate_iterator(code, root, VM_ITR);
}

// <comprehension> --> LEX_LEFTSQUARE <expression> <iterator> LEX_RIGHTSQUARE
void generate_comprehension(struct byte_array *code, struct symbol *root) {
    generate_iterator(code, root, VM_COM);
}

// <trycatch> --> LEX_TRY <statements> LEX_CATCH <variable> <statements> LEX_END
void generate_trycatch(struct byte_array *code, struct symbol *root)
{
    struct byte_array *trial = generate_code(NULL, root->index);

    generate_step(code, 1, VM_TRY);
    serial_encode_string(code, 0, trial);

    serial_encode_string(code, 0, root->token->string);
    struct byte_array *catcher = generate_code(NULL, root->value);
    serial_encode_string(code, 0, catcher);
}

void generate_throw(struct byte_array *code, struct symbol *root)
{
    generate_code(code, root->value);
    generate_step(code, 1, VM_TRO);
}

typedef void(generator)(struct byte_array*, struct symbol*);

struct byte_array *generate_code(struct byte_array *code, struct symbol *root)
{
    if (!root)
        return NULL;
    generator *g = NULL;

    //DEBUGPRINT("generate_code %s\n", nonterminals[root->nonterminal]);
    switch(root->nonterminal) {
        case SYMBOL_NIL:            g = generate_nil;           break;
        case SYMBOL_PAIR:           g = generate_pair;          break;
        case SYMBOL_LOOP:           g = generate_loop;          break;
        case SYMBOL_FDECL:          g = generate_fdecl;         break;
        case SYMBOL_TABLE:          g = generate_list;          break;
        case SYMBOL_FCALL:          g = generate_fcall;         break;
        case SYMBOL_FLOAT:          g = generate_float;         break;
        case SYMBOL_MEMBER:         g = generate_member;        break;
        case SYMBOL_RETURN:         g = generate_return;        break;
        case SYMBOL_SOURCE:         g = generate_source;        break;
        case SYMBOL_STRING:         g = generate_string;        break;
        case SYMBOL_INTEGER:        g = generate_integer;       break;
        case SYMBOL_BOOLEAN:        g = generate_boolean;       break;
        case SYMBOL_ITERLOOP:       g = generate_iterloop;      break;
        case SYMBOL_VARIABLE:       g = generate_variable;      break;
        case SYMBOL_STATEMENTS:     g = generate_statements;    break;
        case SYMBOL_ASSIGNMENT:     g = generate_assignment;    break;
        case SYMBOL_EXPRESSION:     g = generate_math;          break;
        case SYMBOL_DESTINATION:    g = generate_destination;   break;
        case SYMBOL_IF_THEN_ELSE:   g = generate_ifthenelse;    break;
        case SYMBOL_COMPREHENSION:  g = generate_comprehension; break;
        case SYMBOL_TRYCATCH:       g = generate_trycatch;      break;
        case SYMBOL_THROW:          g = generate_throw;         break;
        default:
            return (struct byte_array*)exit_message(ERROR_TOKEN);
    }

    if (!code)
        code = byte_array_new();
    if (g)
        g(code, root);
    return code;
}

struct byte_array *generate_program(struct symbol *root)
{
    DEBUGPRINT("generate:\n");
    struct byte_array *code = byte_array_new();
    generate_code(code, root);
    struct byte_array *program = serial_encode_int(0, 0, code->length);
    byte_array_append(program, code);
#ifdef DEBUG
    display_program(program);
#endif
    return program;
}

// build ///////////////////////////////////////////////////////////////////

struct byte_array *build_string(const struct byte_array *input) {
    assert_message(input, ERROR_NULL);
    struct byte_array *input_copy = byte_array_copy(input);
    DEBUGPRINT("lex %d:\n", input_copy->length);

    lex_list = array_new();
    imports = map_new();
    struct array* list = lex(input_copy);

    struct symbol *tree = parse(list, 0);

    return generate_program(tree);
}

struct byte_array *build_file(const struct byte_array* filename)
{
    struct byte_array *input = read_file(filename);
    return build_string(input);
}

void compile_file(const char* str)
{
    struct byte_array *filename = byte_array_from_string(str);
    struct byte_array *program = build_file(filename);
    struct byte_array *dotfg = byte_array_from_string(EXTENSION_SRC);
    struct byte_array *dotfgbc = byte_array_from_string(EXTENSION_BC);
    int offset = byte_array_find(filename, dotfg, 0);
    assert_message(offset > 0, "invalid source file name");
    byte_array_replace(filename, dotfgbc, offset, dotfg->length);
    write_file(filename, program);
}
