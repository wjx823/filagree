#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <sys/stat.h>

#include "util.h"
#include "struct.h"
#include "serial.h"
#include "compile.h"
#include "vm.h"


#define TAG				"compile"
#define ERROR_LEX		"Lexigraphical error"
#define ERROR_PARSE		"Parsological error"
#define ERROR_TOKEN		"Unknown token"
#define EXTENSION		".kg"
#define QUOTE			'\''
#define ESCAPE			'\\'
#define ESCAPED_NEWLINE	'n'
#define ESCAPED_TAB		't'
#define ESCAPED_QUOTE	'\''

struct array* lex_list;
struct map *imports = NULL;
struct byte_array *read_file(const struct byte_array *filename);

// token ///////////////////////////////////////////////////////////////////

struct token {
	enum Lexeme {
		LEX_NONE,
		LEX_IMPORT,
		LEX_LEFT_COMMENT,
		LEX_RIGHT_COMMENT,
		LEX_PLUS,
		LEX_MINUS,
		LEX_TIMES,
		LEX_DIVIDE,
		LEX_AND,
		LEX_OR,
		LEX_NOT,
		LEX_GREATER,
		LEX_LESSER,
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
		LEX_NUMBER,
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
	} lexeme;
	uint32_t number;
	struct byte_array *string;
};

struct number_string lexemes[] = {
	{LEX_IMPORT,				"import"},
	{LEX_LEFT_COMMENT,			"/*"},
	{LEX_RIGHT_COMMENT,			"*/"},
	{LEX_LINE_COMMENT,			"#"},
	{LEX_PLUS,					"+"},
	{LEX_MINUS,					"-"},
	{LEX_TIMES,					"*"},
	{LEX_DIVIDE,				"/"},
	{LEX_AND,					"and"},
	{LEX_OR,					"or"},
	{LEX_NOT,					"not"},
	{LEX_GREATER,				">"},
	{LEX_LESSER,				"<"},
	{LEX_SAME,					"=="},
	{LEX_SET,					"="},
	{LEX_DIFFERENT,				"!="},
	{LEX_COMMA,					","},
	{LEX_PERIOD,				"."},
	{LEX_COLON,					":"},
	{LEX_LEFTHESIS,				"("},
	{LEX_RIGHTHESIS,			")"},
	{LEX_LEFTSQUARE,			"["},
	{LEX_RIGHTSQUARE,			"]"},
	{LEX_LEFTSTACHE,			"{"},
	{LEX_RIGHTSTACHE,			"}"},
	{LEX_TRUE,					"true"},
	{LEX_FALSE,					"false"},
	{LEX_IF,					"if"},
	{LEX_THEN,					"then"},
	{LEX_ELSE,					"else"},
	{LEX_END,					"end"},
	{LEX_WHILE,					"while"},
	{LEX_FUNCTION,				"function"},
	{LEX_FOR,					"for"},
	{LEX_IN,					"in"},
	{LEX_WHERE,					"where"},
	{LEX_RETURN,				"return"},
};

// display ////////////////////////////////////////////////////////////////////

#ifdef DEBUG

const char* lexeme_to_string(enum Lexeme lexeme)
{
	switch (lexeme) {
		case LEX_IDENTIFIER:return "id";		break;
		case LEX_STRING:	return "string";	break;
		case LEX_NUMBER:	return "number";	break;
		default:
			return num_to_string(lexemes, ARRAY_LEN(lexemes), lexeme);
			break;
	}
	return NULL;
}

void display_token(struct token *token, int depth) {
	assert_message(depth < 10, "kablooie!");
	char* indent = (char*)malloc(sizeof(char)*(depth+1));
	int i;
	for (i=0; i<depth; i++)
		indent[i] = '\t';
	indent[i] = 0;
	DEBUGPRINT("%s%d ", indent, token->lexeme);
	
	switch (token->lexeme) {
		case LEX_NONE:												break;
		case LEX_NUMBER:	DEBUGPRINT("%u", token->number);		break;
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

struct token *token_new(enum Lexeme lexeme) {
	struct token *t = malloc(sizeof(struct token));
	t->lexeme = lexeme;
	t->string = NULL;
	t->number = 0;
	return t;
}

struct token *insert_token(enum Lexeme lexeme) {
	struct token *token = token_new(lexeme);
	array_add(lex_list, token);
	return token;
}

int insert_token_number(const char *input, int i) {
	struct token *token = insert_token(LEX_NUMBER);
	token->number = atoi(&input[i]);
	//while (isdigit(input[i++]));
	for (; isdigit(input[i]); i++);
	return i;
}

bool isiden(char c)
{
	return isalnum(c) || (c=='_');
}

int insert_token_string(enum Lexeme lexeme, const char* input, int i)
{
	struct byte_array *string = byte_array_new();
	while ((lexeme==LEX_IDENTIFIER && isiden(input[i])) || 
		   (lexeme==LEX_STRING && input[i] != QUOTE)) {
		uint8_t c = input[i++];
		DEBUGPRINT("@%d c=%3d %c\n", i, c, c);
		if (c == ESCAPE) {
			c = input[i++];
			switch (c) {
				case ESCAPED_NEWLINE:	c = '\n';						break;
				case ESCAPED_TAB:		c = '\t';						break;
				case ESCAPED_QUOTE:		c = '\'';						break;
				default:				exit_message("unknown escape");	break;
			}
		}
		byte_array_add_byte(string, c);
	}
	
	int end = i + (lexeme==LEX_STRING);
	struct token *token = token_new(lexeme);
	token->string = string;
	array_add(lex_list, token);
	return end;
}

int insert_lexeme(int index) {
	struct number_string sn = lexemes[index];
	insert_token(sn.number);
	return strlen(sn.chars);
}

int import(const char* input, int i)
{
	i += strlen(lexeme_to_string(LEX_IMPORT));
	while (isspace(input[i++]));
	struct byte_array *path = byte_array_new();
	while (!isspace(input[i]) && input[i]!=QUOTE)
		byte_array_add_byte(path, input[i++]);
	byte_array_append(path, byte_array_from_string(EXTENSION));
	if (!map_has(imports, path)) {
		map_insert(imports, path, NULL);
		struct byte_array *imported = read_file(path);
		lex(imported);
	}
	return i+1;
}

struct array* lex(struct byte_array *binput)
{
	DEBUGPRINT("lex:\n");
	lex_list = array_new();
	imports = map_new();
	int i=0,j;
	char c;

	const char* input = byte_array_to_string(binput);
	const char* right_comment = lexeme_to_string(LEX_RIGHT_COMMENT);
	int right_comment_len = strlen(right_comment);
	
lexmore:
	while (i < binput->size) {

		for (j=0; j<ARRAY_LEN(lexemes); j++) {
			char* lexstr = lexemes[j].chars;
			if (!strncmp(&input[i], lexstr, strlen(lexstr))) {
				enum Lexeme lexeme = lexemes[j].number;
				if (lexeme == LEX_LEFT_COMMENT) {
					while (strncmp(&input[i++], right_comment, right_comment_len));
					i += right_comment_len-1;
				}
				else if (lexeme == LEX_LINE_COMMENT)
					while (input[i++] != '\n');
				else if (lexeme == LEX_IMPORT)
					i = import(input, i);
				else {
					if (lexeme == LEX_DIVIDE)
						DEBUGPRINT("lexeme=%d\n",lexeme);
					i += insert_lexeme(j);
				}
				goto lexmore;
			}
		}
		
		c = input[i];
		
		if (isdigit(c))			i = insert_token_number(input,i);
		else if (isiden(c))		i = insert_token_string(LEX_IDENTIFIER, input, i);
		else if (isspace(c))	i++;
		else if (c=='\'')		i = insert_token_string(LEX_STRING, input, i+1);
		else					exit_message(ERROR_LEX);
	}
#ifdef DEBUG
	display_lex_list();
#endif
	DEBUGPRINT("done lexing\n\n");
	return lex_list;
}

/* parse ///////////////////////////////////////////////////////////////////
 
 BNF:
 
 <statements> --> ( <ifthenelse> | <loop> | <rejoinder> | <assignment> | <fcall> )+
 <assignment> --> LEX_IDENTIFIER <member>* LEX_LET_IT_BE ( ( <fdecl> | <expression> ) <dictionary> )^ )
 <ifthenelse> --> IF <expression> THEN <statements>
	(ELSE IF <expression> THEN <statements>)*
	(ELSE <statements>)? LEX_END
 <loop> --> WHILE <expression> <statements> LEX_END
 //<iterator> --> LEX_FOR LEX_IDENTIFIER LEX_IN <expression> ( LEX_WHERE <expression> ) ?
 //<iterloop> --> <iterator> <statements> LEX_END
 //<comprehension> --> LEX_LEFTSQUARE <expression> <iterator> LEX_RIGHTSQUARE
 <table> --> LEX_LEFTSQUARE ( <element> | <pair> ) LEX_RIGHTSQUARE
 <pair> --> <expression> LEX_COLON <expression>
 <rejoinder> --> LEX_RETURN <expression>
 <fdecl> --> FUNCTION LEX_LEFT_PARENTHESIS
	( LEX_IDENTIFIER ( LEX_COMMA LEX_IDENTIFIER )* )?
	LEX_RIGHT_PARENTHESIS <statements> LEX_END
 <fcall> --> <expression> <call>
 <call> --> LEX_LEFT_PARENTHESIS
	( <expression> ( LEX_COMMA <expression> )* )?
	LEX_RIGHT_PARENTHESIS
 
 <expression> --> <exp2> ( ( LEX_SAME | LEX_DIFFERENT | LEX_GREATER | LEX_LESSER ) <exp2> )?
 <exp2> --> <exp3> ( ( LEX_PLUS | LEX_MINUS | LEX_TIMES | LEX_DIVIDE ) <exp3> )*
 <exp3> --> (NOT | LEX_MINUS)? <exp4>
 <exp4> --> <exp5> ( <call> | member )*
 <exp5> --> ( LEX_LEFT_PARENTHESIS <expression> LEX_RIGHT_PARENTHESIS ) | <atom>
 
 <atom> -->  LEX_IDENTIFIER | <float> | <integer> | <table> | <fdecl>
 <integer> --> LEX_INTEGER | LEX_TRUE | LEX_FALSE
 <floater> --> LEX_INTEGER LEX_PERIOD LEX_INTEGER
 <string> --> LEX_STRING
 <member> --> LEX_PERIOD <identifier>
 
 *///////////////////////////////////////////////////////////////////////////

#define LHS					0
#define RHS					1
#define BRANCH_ITERATOR		1
#define BRANCH_ITERATION	0
#define BRANCH_VAR			0
#define BRANCH_TABLE		0
#define BRANCH_WHERE		1
#define FNC_BODY			1
#define INDEX				0
#define ITERABLE			1

struct symbol {
	enum Nonterminal {
		SYMBOL_STATEMENTS,
		SYMBOL_ASSIGNMENT,
		SYMBOL_IF_THEN_ELSE,
        SYMBOL_LOOP,
		SYMBOL_EXPRESSION,
		SYMBOL_ATOM,
		SYMBOL_INTEGER,
		SYMBOL_FLOAT,
		SYMBOL_STRING,
		SYMBOL_VARIABLE,
		SYMBOL_TABLE,
		SYMBOL_PAIR,
		SYMBOL_FDECL,
		SYMBOL_FCALL,
		SYMBOL_MEMBER,
		SYMBOL_LENGTH,
		SYMBOL_RETURN,
	} nonterminal;
	const struct token *token;
	float floater;
	struct array* branches;
	bool is_lhs;
};

struct number_string nonterminals[] = {
	{SYMBOL_STATEMENTS,		"statements"},
	{SYMBOL_ASSIGNMENT,		"assignment"},
	{SYMBOL_IF_THEN_ELSE,	"if-then-else"},
    {SYMBOL_LOOP,			"loop"},
	{SYMBOL_EXPRESSION,		"expression"},
	{SYMBOL_ATOM,			"atom"},
	{SYMBOL_INTEGER,		"integer"},
	{SYMBOL_FLOAT,			"float"},
	{SYMBOL_STRING,			"string"},
	{SYMBOL_VARIABLE,		"variable"},
	{SYMBOL_TABLE,			"table"},
	{SYMBOL_PAIR,			"pair"},
	{SYMBOL_FDECL,			"fdecl"},
	{SYMBOL_FCALL,			"fcall"},
	{SYMBOL_MEMBER,			"member"},
	{SYMBOL_LENGTH,			"length"},
	{SYMBOL_RETURN,			"return"},
};

struct symbol *symbol_new(enum Nonterminal nonterminal)
{
	//	DEBUGPRINT("symbol_new %s\n", num_to_string(nonterminals, ARRAY_LEN(nonterminals), nonterminal));
	struct symbol *s = malloc(sizeof(struct symbol));
	s->nonterminal = nonterminal;
	s->branches = array_new();
	s->is_lhs = false;
	return s;
}

struct symbol *symbol_add(struct symbol *s, struct symbol *t)
{
	null_check(s);
	if (!t)
		return NULL;
	//DEBUGPRINT("symbol_add %s\n", nonterminals[t->nonterminal]);
	array_add(s->branches, t);
	return s;
}

#ifdef DEBUG

void display_symbol(const struct symbol *symbol, int depth) {
	null_check(symbol);
	assert_message(depth < 100, "kablooie!");
	char* indent = (char*)malloc(sizeof(char)*(depth+1));
	int i;
	for (i=0; i<depth; i++)
		indent[i] = '\t';
	indent[i] = 0;
	DEBUGPRINT("%s%d ", indent, symbol->nonterminal);
	
	switch (symbol->nonterminal) {
		case SYMBOL_INTEGER:
			DEBUGPRINT("%u", symbol->token->number);
			break;
		case SYMBOL_FLOAT:
			DEBUGPRINT("%f", symbol->floater);
			break;
		case SYMBOL_VARIABLE:
		case SYMBOL_STRING: {
			char* s = byte_array_to_string(symbol->token->string);
			DEBUGPRINT("'%s'", s);
			free(s);
		} break;
		case SYMBOL_EXPRESSION:
			DEBUGPRINT("%s %s",
					   num_to_string(nonterminals, ARRAY_LEN(nonterminals), symbol->nonterminal),
					   lexeme_to_string(symbol->token->lexeme));
			break;
		default:
			DEBUGPRINT("%s", num_to_string(nonterminals, ARRAY_LEN(nonterminals), symbol->nonterminal));
			break;
	}
	DEBUGPRINT("\n");
	
	depth++;
	const struct array *branches = symbol->branches;
	for (int k=0; k<branches->length; k++) {
		const struct symbol *branch = array_get(branches, k);
		display_symbol(branch, depth);
	}
	
	free(indent);
}

#endif

struct array* parse_list;
uint32_t parse_index;
struct symbol *expression();
struct symbol *statements();
struct symbol *comprehension();
#define LOOKAHEAD (lookahead(0))
#define FETCH_OR_QUIT(x) if (!fetch(x)) return NULL;


enum Lexeme lookahead(int n) {
	if (parse_index + n >= parse_list->length)
		return LEX_NONE;
	struct token *token = parse_list->data[parse_index+n];
	assert_message(token!=0, ERROR_NULL);
	return token->lexeme;
}

struct token *fetch(enum Lexeme lexeme) {
	if (parse_index >= parse_list->length)
		return NULL;
	struct token *token = parse_list->data[parse_index];
	if (token->lexeme != lexeme)
		return NULL;
	DEBUGPRINT("fetched %s at %d\n", lexeme_to_string(lexeme), parse_index);
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
	
	for (; lexeme; lexeme = va_arg(argp, enum Lexeme)) {
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
	struct token *token = parse_list->data[parse_index];
	assert_message(token!=0, ERROR_NULL);
	enum Lexeme lexeme = token->lexeme;
	
	struct symbol *symbol = NULL;
	
	va_list argp;
	va_start(argp, goal);
	
	for (; goal; goal = va_arg(argp, enum Lexeme)) {
		if (lexeme == goal) {
			
			symbol = symbol_new(n);
			symbol->token = token;
			DEBUGPRINT("fetched %s at %d\n", lexeme_to_string(lexeme), parse_index);
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
		array_add(s->branches, child);
	va_end(argp);
	return s;
	
}

//////////////////////////////// BNFs

struct symbol *element(bool colonizable)
{
	DEBUGPRINT("element @ %d\n", parse_index);
	struct symbol *e = expression();
	if (colonizable && fetch(LEX_COLON)) { // i.e., x:y
		struct symbol *f = expression();
		struct symbol *p = symbol_new(SYMBOL_PAIR);
		e = symbol_adds(p, f, e, NULL);
	}
	return e;
}

struct symbol *list(enum Nonterminal nonterminal,
					enum Lexeme start,
					enum Lexeme stop,
					bool colonizable) {
	FETCH_OR_QUIT(start);
	struct symbol *s = symbol_new(nonterminal);
	
	if (LOOKAHEAD!=stop) {
		symbol_adds(s, element(colonizable), NULL);
		while (fetch(LEX_COMMA))
			symbol_adds(s, element(colonizable), NULL);
	}
	if (LOOKAHEAD!=stop) {
		int c = LOOKAHEAD;
		DEBUGPRINT("bad list: %d\n", c);
		exit_message("bad list");
		return NULL;
	}
	fetch(stop);
	return s;
}

struct symbol *fdecl() {
	FETCH_OR_QUIT(LEX_FUNCTION)
	struct symbol *s = list(SYMBOL_FDECL, LEX_LEFTHESIS, LEX_RIGHTHESIS, false);
	symbol_add(s, statements());
	fetch(LEX_END);
	return s;
}

struct symbol *table() {
	//	DEBUGPRINT("table\n");
	return list(SYMBOL_TABLE, LEX_LEFTSQUARE, LEX_RIGHTSQUARE, true);
}

struct symbol *integer()
{
	//	DEBUGPRINT("integer\n");
	struct token *t = fetch_lookahead(LEX_NUMBER, LEX_TRUE, LEX_FALSE, NULL);
	if (!t)
		return NULL;
	struct symbol *s = symbol_new(SYMBOL_INTEGER);
	s->token = t;
	return s;
}

struct symbol *floater()
{
	//	DEBUGPRINT("floater\n");
	struct token *a = fetch(LEX_NUMBER);
	if (!a)
		return NULL;
	
	FETCH_OR_QUIT(LEX_PERIOD);
	
	struct token *b = fetch(LEX_NUMBER);
	if (!b)
		return NULL;
	
	struct symbol *s = symbol_new(SYMBOL_FLOAT);
	float decimal;
	for (decimal = b->number; decimal>1; decimal /= 10);	
	s->floater = a->number + decimal;
	return s;
}

// <string> --> LEX_STRING
struct symbol *string()
{
	//	DEBUGPRINT("string\n");
	struct token *t = fetch(LEX_STRING);
	if (!t)
		return NULL;
	struct symbol *s = symbol_new(SYMBOL_STRING); // todo: add lexeme as second parameter
	s->token = t;
	return s;
}

struct symbol *variable()
{
	//	DEBUGPRINT("variable\n");
	if (LOOKAHEAD != LEX_IDENTIFIER)
		return NULL;
	struct token *n = fetch(LEX_IDENTIFIER);
	struct symbol *s = symbol_new(SYMBOL_VARIABLE);
	s->token = n;
	return s;
}

// <atom> -->  LEX_IDENTIFIER | <string> | <integer> | <floater> | <table>
struct symbol *atom()
{
	return one_of(&variable, &string, &floater, &integer, &table, &fdecl, NULL);
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

// <member> --> LEX_PERIOD <identifier> | LEX_LEFTSQUARE <expression> LEX_RIGHTSQUARE
struct symbol *member()
{
	if (fetch(LEX_PERIOD)) {
		struct symbol *e = variable();
		if (!e)
			return NULL;
		e->nonterminal = SYMBOL_STRING;
		struct symbol *m = symbol_new(SYMBOL_MEMBER);
		symbol_add(m, e);
		return m;
	}
	FETCH_OR_QUIT(LEX_LEFTSQUARE);
	struct symbol *e = expression();
	FETCH_OR_QUIT(LEX_RIGHTSQUARE);
	struct symbol *m = symbol_new(SYMBOL_MEMBER);
	return symbol_adds(m, e, NULL);
}

// <call> --> LEX_LEFT_PARENTHESIS ( <expression> ( LEX_COMMA <expression> )* )? LEX_RIGHT_PARENTHESIS
struct symbol *call()
{
	return list(SYMBOL_FCALL, LEX_LEFTHESIS, LEX_RIGHTHESIS, false);
}

// <fcall> --> <expression> <call>
struct symbol *fcall()
{
	struct symbol *e = expression();
	return e && e->nonterminal ==  SYMBOL_FCALL ? e : NULL;
}

// <exp4> --> <exp5> ( <call> | member )*
struct symbol *exp4()
{
	struct symbol *g, *f;
	f = exp5();
	while (f && (g = one_of(&call, &member, NULL)))
		f = symbol_adds(g, f, NULL);
	return f;
}

// <exp3> --> (NOT | LEX_MINUS)? <exp4>
struct symbol *exp3()
{
	struct symbol *e;
	if ((e = symbol_fetch(SYMBOL_EXPRESSION, LEX_MINUS, LEX_NOT, NULL)))
		return symbol_adds(e, exp3(), NULL);
	return exp4();
}

// <exp2> --> (<exp3> ( ( LEX_PLUS | LEX_MINUS | LEX_TIMES | LEX_DIVIDE | MODULO ))* <exp3>
struct symbol *expTwo()
{
	struct symbol *e, *f;
	e = exp3();
	while ((f = symbol_fetch(SYMBOL_EXPRESSION, LEX_PLUS, LEX_MINUS, LEX_TIMES, LEX_DIVIDE, NULL)))
		e = symbol_adds(f, e, exp3(), NULL);
	return e;
}

// <expression> --> <exp2> ( ( LEX_SAME | LEX_DIFFERENT | LEX_GREATER | LEX_LESSER ) <exp2> )?
struct symbol *expression()
{
	struct symbol *f, *e = expTwo();
	while ((f = symbol_fetch(SYMBOL_EXPRESSION, LEX_SAME, LEX_DIFFERENT, LEX_GREATER, LEX_LESSER, NULL)))
		e = symbol_adds(f, e, expTwo(), NULL);
	return e;
}

// <assignment> --> <variable> <member>* LEX_LET_IT_BE ( <fdecl> | <expression> )
struct symbol *assignment()
{
	struct symbol *m, *v = variable();
	if (!v)
		return NULL;
	while ((m = member()))
		v = symbol_adds(m, v, NULL);
	FETCH_OR_QUIT(LEX_SET);
	
	struct symbol *s = symbol_new(SYMBOL_ASSIGNMENT);
	symbol_adds(s, v, NULL);
	return symbol_adds(s, one_of(&expression, &fdecl), NULL);
}

/*	<ifthenelse> --> IF <expression> THEN <statements>
 (ELSE IF <expression> THEN <statements>)*
 (ELSE <statements>)? END */
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
	
	if (fetch_lookahead(LEX_ELSE))
		symbol_add(f, statements());
	fetch(LEX_END);
	return f;
}

// <loop> --> WHILE <expression> <statements> END
struct symbol *loop()
{
	FETCH_OR_QUIT(LEX_WHILE);
    struct symbol *s = symbol_new(SYMBOL_LOOP);
    symbol_add(s, expression());
    symbol_add(s, statements());
    fetch(LEX_END);
    return s;
}
/*
 // <iterator> --> LEX_FOR LEX_IDENTIFIER LEX_IN <expression> ( LEX_WHERE <expression> )?
 struct symbol *iterator()
 {
 FETCH_OR_QUIT(LEX_FOR);
 const struct token *t = fetch(LEX_IDENTIFIER);
 struct symbol *s = symbol_new(SYMBOL_ITERATOR);
 s->token = t;
 
 fetch(LEX_IN);
 symbol_add(s, expression());
 symbol_add(s, fetch_lookahead(LEX_WHERE) ? expression() : NULL);
 return s;
 }
 
 // <comprehension> --> LEX_LEFTSQUARE <expression> <iterator> LEX_RIGHTSQUARE
 struct symbol *comprehension()
 {
 //	DEBUGPRINT("comprehension\n");
 if (LOOKAHEAD != LEX_LEFTSQUARE)
 return NULL;
 enum Lexeme lan;
 for (int n=1; (lan = lookahead(n))!=LEX_FOR; n++) {
 if (lan==LEX_RIGHTSQUARE)
 return NULL;
 else if (lan==LEX_NONE)
 exit_message("bad iterable");
 }
 
 fetch(LEX_LEFTSQUARE);
 struct symbol *s = symbol_new(SYMBOL_COMPREHENSION);
 symbol_add(s, expression());
 symbol_add(s, iterator());
 fetch(LEX_RIGHTSQUARE);
 return s;
 }
 
 // <iterloop> --> <iterator> <statements> LEX_END
 struct symbol *iterloop()
 {
 struct symbol *i = iterator();
 if (!i)
 return  NULL;
 struct symbol *s = symbol_new(SYMBOL_ITERLOOP);
 symbol_adds(s, statements(), i, NULL);
 fetch(LEX_END);
 return s;
 }
 */

// <rejoinder> --> // LEX_RETURN <expression>
struct symbol *rejoinder()
{
	//	DEBUGPRINT("rejoinder\n")
	FETCH_OR_QUIT(LEX_RETURN);
	struct symbol *s = symbol_new(SYMBOL_RETURN);
	return symbol_adds(s, expression(), NULL);
}

// <statements> --> ( <import> | <ifthenelse> | <loop> | <iterloop> | | <rejoinder> | <assignment> | <fcall> )+
struct symbol *statements()
{
	struct symbol *s = symbol_new(SYMBOL_STATEMENTS);
	struct symbol *t;
	while ((t = one_of(&assignment, &fcall, &ifthenelse, &loop, &rejoinder, NULL)))
		symbol_add(s, t);
	return s;
}

struct symbol *parse(struct array *list, uint32_t index)
{ // todo: enforce syntax consistently
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

typedef struct byte_array*(code_generator)(struct symbol*);
struct byte_array *generate_code(struct symbol *root);
struct byte_array *build_file(const struct byte_array* filename);


struct byte_array *encodeInt(int n) {
	struct byte_array *num = byte_array_new();
	num->size = 1;
	num->data = malloc(sizeof(uint8_t));
	num->data[0] = (uint8_t)n; // todo: support larger numbers
	return num;
}

struct byte_array *generate_step(int count, int action,...)
{
	
	struct byte_array *bytecode = byte_array_new_size(1);
	bytecode->data[0] = (uint8_t)action;
	
	va_list argp;
	uint8_t parameter;	
	for(va_start(argp, action); --count;) {
		parameter = va_arg(argp, int);
		byte_array_add_byte(bytecode, (uint8_t)parameter);
	}
	
	va_end(argp);
	
	return bytecode;
}

struct byte_array *generate_statements(struct symbol *root)
{
	if (!root)
		return NULL;
	struct byte_array *code = byte_array_new();
	for (int i=0; i<root->branches->length; i++) {
		struct symbol *branch = array_get(root->branches, i);
		struct byte_array *branch_code = generate_code(branch);
		byte_array_append(code, branch_code);
	}
	return code;
}

struct byte_array *generate_math(struct symbol *root)
{
	enum Lexeme lexeme = root->token->lexeme;
	struct byte_array *code = generate_statements(root);
	enum Opcode op;
	switch (lexeme) {
		case LEX_PLUS:			op = VM_ADD;			break;
		case LEX_MINUS:			op = VM_SUB;			break;
		case LEX_TIMES:			op = VM_MUL;			break;
		case LEX_DIVIDE:		op = VM_DIV;			break;
		case LEX_AND:			op = VM_AND;			break;
		case LEX_OR:			op = VM_OR;				break;
		case LEX_NOT:			op = VM_NOT;			break;
		case LEX_SAME:			op = VM_EQ;				break;
		case LEX_DIFFERENT:		op = VM_NEQ;			break;
		case LEX_GREATER:		op = VM_GT;				break;
		case LEX_LESSER:		op = VM_LT;				break;			
		default:	exit_message("bad math lexeme");	break;
	}
	byte_array_append(code, generate_step(1,op));
	return code;
}

static inline struct byte_array *generate_jump(uint32_t offset)
{
	return generate_step(2, VM_JMP, offset);
}

struct byte_array *generate_ifthenelse(struct symbol *root)
{
	struct array *gotos = array_new(); // for skipping to end of elseifs, if one is true 
	struct byte_array *code = byte_array_new();

	for (int i=0; i<root->branches->length; i+=2) {

		// then
		struct symbol *thn = array_get(root->branches, i+1);
		assert_message(thn->nonterminal == SYMBOL_STATEMENTS, "branch syntax error");
		struct byte_array *thn_code = generate_code(thn);
		byte_array_append(thn_code, generate_jump(0)); // jump to end
		
		// if
		struct symbol *iff = array_get(root->branches, i);
		byte_array_append(code, generate_code(iff));
		byte_array_append(code, generate_step(2, VM_IF, thn_code->size));
		byte_array_append(code, thn_code);
		array_add(gotos, (void*)(VOID_INT)(code->size-1));
		
		// else
		if (root->branches->length > i+2) {
			struct symbol *els = array_get(root->branches, i+2);
			if (els->nonterminal == SYMBOL_STATEMENTS) {
				assert_message(root->branches->length == i+3, "else should be the last branch");
				struct byte_array *els_code = generate_code(els);
				byte_array_append(code, els_code);
				break;
			}
		}
	}
	
	// go back and fill in where to jump to the end of if-then-elseif-else when done
	for (int j=0; j<gotos->length; j++) {
		VOID_INT g = (VOID_INT)array_get(gotos, j);
		code->data[g] = code->size - g;
	}
	return code;
}

struct byte_array *generate_loop(struct symbol *root)
{
	struct byte_array *ifa = generate_code(array_get(root->branches, 0));
	struct byte_array *b   = generate_code(array_get(root->branches, 1));    
	struct byte_array *thn = generate_step(2,
										   VM_IF,
										   b->size + 2);
    uint8_t loop_length = ifa->size + thn->size + b->size;
    struct byte_array *again = generate_jump(-loop_length-1); // todo: handle large jumps
	struct byte_array *loop = byte_array_concatenate(4, ifa, thn, b, again); // if a then b, repeat
    return loop;
}

struct byte_array *generate_float(struct symbol *root)
{
	struct byte_array *step = generate_step(1, VM_FLT);
	serial_encode_float(step, 0, root->floater);
	return step;
}

struct byte_array *generate_int(struct symbol *root) {
	uint32_t value;
	switch (root->token->lexeme) {
		case LEX_TRUE:	value = 1;						break;
		case LEX_FALSE:	value = 0;						break;
		default:		value = root->token->number;	break;
	}
	return generate_step(2, VM_INT, value);
}

struct byte_array *generate_string(struct symbol *root) {
	struct byte_array *step = generate_step(1, VM_STR);
	struct byte_array *name = serial_encode_string(0, 0, root->token->string);
	struct byte_array *a = byte_array_concatenate(2, step, name);
	return a;
}

struct byte_array *generate_assignment(struct symbol *root)
{
	struct symbol *lhs = array_get(root->branches, LHS);
	lhs->is_lhs = true;
	struct symbol *rhs = array_get(root->branches, RHS);
	struct byte_array *lhs_code = generate_code(lhs);
	struct byte_array *rhs_code = generate_code(rhs);
	
	struct byte_array *a = byte_array_concatenate(2, rhs_code, lhs_code);
	return a;
}

struct byte_array *generate_fdecl(struct symbol *root)
{
	struct byte_array *code = byte_array_new();
	struct array *args = root->branches;
	for (int i=args->length-2; i>=0; i--) {
		struct symbol *arg = array_get(args, i);
		struct byte_array *name = serial_encode_string(0, 0, arg->token->string);
		byte_array_append(code, generate_step(1,VM_SET));
		byte_array_append(code, name);
		
	}
	struct symbol *body = array_get(root->branches, root->branches->length-1);
	byte_array_append(code, generate_code(body));
	struct byte_array *code_str = serial_encode_string(0, 0, code);
	
	struct byte_array *op = generate_step(1, VM_FNC);
	struct byte_array *fd = byte_array_concatenate(2, op, code_str);
	return fd;
}

struct byte_array *generate_variable(struct symbol *root)
{
	struct byte_array *op;;
	op = generate_step(1, root->is_lhs ? VM_SET : VM_VAR);
	struct byte_array *name = serial_encode_string(0, 0, root->token->string);
	byte_array_append(op, name);
	return op;
}

struct byte_array *generate_array_items(const struct array* items)
{
	struct byte_array *step, *result = byte_array_new();
	for (int i=0; i<items->length; i++) {
		struct symbol *item = (struct symbol*)array_get(items, i);
		step = generate_code(item);
		byte_array_append(result, step);
	}
	return result;
}

struct byte_array *generate_pair(struct symbol *root)
{
	struct byte_array *items = generate_array_items(root->branches);
	struct byte_array *op = generate_step(2, VM_MAP, root->branches->length/2);
	struct byte_array *list = byte_array_concatenate(2, items, op);
    return list;
}

struct byte_array *generate_list(struct symbol *root)
{
	struct byte_array *items = generate_array_items(root->branches);
	struct byte_array *op = generate_step(2, VM_LST, root->branches->length);
	struct byte_array *list = byte_array_concatenate(2, items, op);
    return list;
}

struct  byte_array* generate_member(struct symbol *root)
{
	enum Opcode op = root->is_lhs ? VM_PUT : VM_GET;
	struct byte_array *access_code = generate_step(1, op);
	struct symbol *iterable = array_get(root->branches, ITERABLE);
	struct symbol *index = array_get(root->branches, INDEX);
	struct byte_array *iterable_code = generate_code(iterable);
	struct byte_array *index_code = generate_code(index);
	//	byte_array_print_bytes("index", index_code);
	//	byte_array_print_bytes("iterable", iterable_code);
	//	byte_array_print_bytes("access", access_code);
	struct byte_array *result = byte_array_concatenate(3, index_code, iterable_code, access_code);
	//	byte_array_print_bytes("result", result);
	return result;
}

struct byte_array *generate_fcall(struct symbol *root)
{
	struct byte_array *op = generate_step(1, VM_CAL);
	struct byte_array *params = generate_array_items(root->branches);
	return byte_array_concatenate(2, params, op);
}

struct byte_array *generate_return(struct symbol *root)
{
	struct byte_array *code = generate_code(array_get(root->branches,0));
	// todo: put multiple return items into an array
	return code;
}

struct byte_array *generate_code(struct symbol *root)
{
	if (root==0)
		return NULL;
	
	struct byte_array *code = NULL;
	code_generator *codegen;
	
	//DEBUGPRINT("generate_code %s\n", nonterminals[root->nonterminal]);
	switch(root->nonterminal) {
		case SYMBOL_STATEMENTS:		return generate_statements(root);
		case SYMBOL_ASSIGNMENT:		return generate_assignment(root);
		case SYMBOL_EXPRESSION:		return generate_math(root);
		case SYMBOL_INTEGER:		return generate_int(root);
		case SYMBOL_FLOAT:			return generate_float(root);
		case SYMBOL_STRING:			return generate_string(root);
		case SYMBOL_VARIABLE:		return generate_variable(root);
		case SYMBOL_IF_THEN_ELSE:	return generate_ifthenelse(root);
		case SYMBOL_FCALL:			return generate_fcall(root);
		case SYMBOL_MEMBER:			return generate_member(root);
		case SYMBOL_FDECL:			return generate_fdecl(root);
		case SYMBOL_PAIR:			return generate_pair(root);
		case SYMBOL_TABLE:			return generate_list(root);
        case SYMBOL_LOOP:			return generate_loop(root);
		case SYMBOL_RETURN:			return generate_return(root);
		default:
			exit_message(ERROR_TOKEN);	
			break;
	}
	code = codegen(root);
	//	DEBUGPRINT("generate_code %s\n", nonterminals[root->nonterminal]);
	//	byte_array_print_bytes("code", code);
	return code;
}

struct byte_array *generate(struct symbol *root)
{
	DEBUGPRINT("generate:\n");
	struct byte_array *code = generate_code(root);
	struct byte_array *program = serial_encode_int(0, 0, code->size);
	byte_array_append(program, code);
#ifdef DEBUG
	display_program(0, program);
#endif
	return program;
}	

// build ///////////////////////////////////////////////////////////////////

struct byte_array *build_string(const struct byte_array *input) {
	assert_message(input!=0, ERROR_NULL);
	struct byte_array *input_copy = byte_array_copy(input);
	struct array* list = lex(input_copy);
	struct symbol *tree = parse(list, 0);
	return generate(tree);
}

struct byte_array *build_file(const struct byte_array* filename)
{
	struct byte_array *input = read_file(filename);
	return build_string(input);
}



#define ERROR_USAGE	"usage: main <script filename>"

// file


#ifdef FILE_RW


#define INPUT_MAX_LEN	10000
#define ERROR_FSIZE		"Could not get length of file"
#define ERROR_ALLOC		"Could not allocate input buffer"
#define ERROR_FOPEN		"Could not open file"
#define ERROR_FREAD		"Could not read file"
#define ERROR_FCLOSE	"Could not close file"
#define ERROR_BIG		"Input file is too big"


long fsize(FILE * file) {
	if (!fseek(file, 0, SEEK_END)) {
		long size = ftell(file);
		if (size >= 0 && !fseek(file, 0, SEEK_SET))
			return size;
	}
	return -1;
}

struct byte_array *read_file(const struct byte_array *filename_ba) {
	FILE * file;
	size_t read;
	uint8_t *str;
	long size;
	
	struct stat st;
	const char* filename_str = byte_array_to_string(filename_ba);
	if (stat(filename_str, &st)) {
		DEBUGPRINT("%s does not exist\n", filename_str);
		return 0;
	}
	
	if (!(file = fopen(filename_str, "rb")))
		exit_message(ERROR_FOPEN);
	if ((size = fsize(file)) < 0)
		exit_message(ERROR_FSIZE);
	else if (size > INPUT_MAX_LEN)
		exit_message(ERROR_BIG);
	if (!(str = malloc((size_t)size)))// + 1)))
		exit_message(ERROR_ALLOC);
	
	read = fread(str, 1, (size_t)size, file);
	if (feof(file) || ferror(file))
		exit_message(ERROR_FREAD);
	//str[read] = 0;
	
	if (fclose(file))
		exit_message(ERROR_FCLOSE);
	
	struct byte_array* ba = byte_array_new_size(read);
	ba->data = ba->current = str;
	return ba;
}

int write_byte_array(struct byte_array* ba, FILE* file) {
	uint16_t len = ba->size;
	int n = fwrite(ba->data, 1, len, file);
	return len - n;
}

int write_file(const char* filename, struct byte_array* bytes)
{
	DEBUGPRINT("write_file\n");
	FILE* file = fopen(filename, "w");
	if (!file) {
		DEBUGPRINT("could not open file %s\n", filename);
		return -1;
	}
	//struct byte_array* bytes = byte_array_new();
	//serial_encode_string(bytes, 0, data);
	int r = fwrite(bytes->data, 1, bytes->size, file);
	DEBUGPRINT("\twrote %d bytes\n", r);
	int s = fclose(file);
	DEBUGPRINT("write_file done\n");
	return (r<0) || s;
}

char* build_path(const char* dir, const char* name)
{
	int dirlen = dir ? strlen(dir) : 0;
	char* path = malloc(dirlen + 1 + strlen(name));
	const char* slash = (dir && dirlen && (dir[dirlen] != '/')) ? "/" : "";
	sprintf(path, "%s%s%s", dir ? dir : "", slash, name);
	return path;
}

#endif // FILE_RW

// main: read file, build, run /////////////////////////////////////////////

struct variable *interpret_string(const char* str, bridge *callback)
{
	struct byte_array *input = byte_array_from_string(str);
	struct byte_array *program = build_string(input);
	return execute(program, 0);
}

struct variable *interpret_file(const char* str, bridge *callback)
{
	struct byte_array *filename = byte_array_from_string(str);
	struct byte_array *program = build_file(filename);
	return execute(program, NULL);
}

#ifndef TEST

int main (int argc, char** argv)
{
	if (argc != 2)
		exit_message(ERROR_USAGE);
	interpret_file(argv[1], &default_callback);
}

#endif // TEST