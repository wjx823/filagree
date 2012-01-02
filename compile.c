#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>

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

uint32_t line;
struct array* lex_list;
struct map *imports = NULL;
struct byte_array *read_file(const struct byte_array *filename);

// todo: enable cast between some primitives: flt->int, etc.
// todo: table concat, insert, remove

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
	} lexeme;
	uint32_t number;
	struct byte_array *string;
	uint32_t at_line;
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
		case LEX_INTEGER:	return "number";	break;
		default:
			return NUM_TO_STRING(lexemes, lexeme);
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
		case LEX_INTEGER:	DEBUGPRINT("%u", token->number);		break;
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
	struct token *t = malloc(sizeof(struct token));
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
				case ESCAPED_NEWLINE:	c = '\n';						break;
				case ESCAPED_TAB:		c = '\t';						break;
				case ESCAPED_QUOTE:		c = '\'';						break;
				default:				exit_message("unknown escape");	break;
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
	lex_list = array_new();
	imports = map_new();
	int i=0,j;
	char c;
	line = 1;

	const char* input = byte_array_to_string(binput);
	const char* right_comment = lexeme_to_string(LEX_RIGHT_COMMENT);
	int right_comment_len = strlen(right_comment);

lexmore:
	while (i < binput->size) {

		for (j=0; j<ARRAY_LEN(lexemes); j++) {

			char* lexstr = lexemes[j].chars;

			if (!strncmp(&input[i], lexstr, strlen(lexstr))) {

				enum Lexeme lexeme = lexemes[j].number;

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
		else if (c == '\n')		{ line++; i++; }
		else if (isspace(c))	i++;
		else if (c=='\'')		i = insert_token_string(LEX_STRING, input, i+1);
		else					exit_message(ERROR_LEX);
	}
#ifdef DEBUG
	display_lex_list();
#endif
	return lex_list;
}

/* parse ///////////////////////////////////////////////////////////////////

 BNF:

 <statements> --> ( <assignment> | <fcall> | <ifthenelse> | <loop> | <rejoinder> )*
 <assignment> --> <destination>, LEX_SET <source>,
 <destination> --> <variable> | ( <expression> <member> )
 <source> --> <expression>

 <ifthenelse> --> IF <expression> THEN <statements>
				  ( ELSE IF <expression> THEN <statements> )*
				  ( ELSE <statements>)? LEX_END
 <loop> --> WHILE <expression> <statements> LEX_END
 //<iterator> --> LEX_FOR LEX_IDENTIFIER LEX_IN <expression> ( LEX_WHERE <expression> )?
 //<iterloop> --> <iterator> <statements> LEX_END
 //<comprehension> --> LEX_LEFTSQUARE <expression> <iterator> LEX_RIGHTSQUARE

 <rejoinder> --> LEX_RETURN <source>,
 <fdecl> --> FUNCTION LEX_LEFT_PARENTHESIS <destination>, LEX_RIGHT_PARENTHESIS <statements> LEX_END
 <fcall> --> <expression> <call>
 <call> --> LEX_LEFT_PARENTHESIS <source>, LEX_RIGHT_PARENTHESIS

 <expression> --> <exp2> ( ( LEX_SAME | LEX_DIFFERENT | LEX_GREATER | LEX_LESSER ) <exp2> )?
 <exp2> --> <exp3> ( ( LEX_PLUS | LEX_MINUS | LEX_TIMES | LEX_DIVIDE ) <exp3> )*
 <exp3> --> (NOT | LEX_MINUS)? <exp4>
 <exp4> --> <exp5> ( <call> | <member> )*
 <exp5> --> ( LEX_LEFT_PARENTHESIS <expression> LEX_RIGHT_PARENTHESIS ) |
			<variable> | <float> | <integer> | <boolean> | <table> | <fdecl>

 <variable> --> LEX_IDENTIFIER
 <boolean> --> LEX_TRUE | LEX_FALSE
 <floater> --> LEX_INTEGER LEX_PERIOD LEX_INTEGER
 <string> --> LEX_STRING
 <table> --> LEX_LEFTSQUARE <element>, LEX_RIGHTSQUARE
 <element> --> <expression> ( LEX_COLON <expression> )?
 <member> --> ( LEX_LEFTSQUARE <expression> LEX_RIGHTSQUARE ) | ( LEX_PERIOD LEX_STRING )

 *///////////////////////////////////////////////////////////////////////////

struct symbol {
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
		SYMBOL_LENGTH,
		SYMBOL_RETURN,
		SYMBOL_BOOLEAN,
		SYMBOL_NIL,
	} nonterminal;
	const struct token *token;
	struct array* list;
	struct symbol *index, *value;
	float floater;
	bool lhs;
};

struct number_string nonterminals[] = {
	{SYMBOL_STATEMENTS,		"statements"},
	{SYMBOL_ASSIGNMENT,		"assignment"},
	{SYMBOL_SOURCE,			"source"},
	{SYMBOL_DESTINATION,	"destination"},
	{SYMBOL_IF_THEN_ELSE,	"if-then-else"},
	{SYMBOL_LOOP,			"loop"},
	{SYMBOL_EXPRESSION,		"expression"},
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
	{SYMBOL_BOOLEAN,		"boolean"},
	{SYMBOL_NIL,			"nil"},
};

struct array* parse_list;
uint32_t parse_index;
struct symbol *expression();
struct symbol *statements();
struct symbol *comprehension();

struct symbol *symbol_new(enum Nonterminal nonterminal)
{
	//	DEBUGPRINT("symbol_new %s\n", NUM_TO_STRING(nonterminals, ARRAY_LEN(nonterminals), nonterminal));
	struct symbol *s = malloc(sizeof(struct symbol));
	s->nonterminal = nonterminal;
	s->list = array_new();
	s->index = s->value = NULL;
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
	//	null_check(symbol);
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
		case SYMBOL_VARIABLE:
		case SYMBOL_STRING: {
			char* s = byte_array_to_string(symbol->token->string);
			DEBUGPRINT(": '%s'\n", s);
			free(s);
		} break;
		case SYMBOL_EXPRESSION:
			DEBUGPRINT("%s\n", lexeme_to_string(symbol->token->lexeme));
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
#define FETCH_OR_ERROR(x) if (!fetch(x)) { exit_message("missing %s at line %d", NUM_TO_STRING(lexemes, x), line); return NULL; }


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
	//	DEBUGPRINT("fetched %s at %d\n", lexeme_to_string(lexeme), parse_index);
	display_token(token, 0);

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
			// DEBUGPRINT("fetched %s at %d\n", lexeme_to_string(lexeme), parse_index);
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

/*
//  <destination> --> <expression> ( LEX_COLON <expression> )?
struct symbol *destination()
{
	struct symbol *e = expression();
	if (!e)
		return NULL;
	struct symbol *p = symbol_new(SYMBOL_PAIR);
	p->index = e;
	p->index->lhs = true;

	if (fetch(LEX_COLON)) // i.e. x:y
		p->value = expression();
	else
		p->value = symbol_new(SYMBOL_NIL);
	return p;
}
*/

//<variable> --> LEX_IDENTIFIER
struct symbol *variable()
{
	return symbol_fetch(SYMBOL_VARIABLE, LEX_IDENTIFIER, NULL);
}


// <fdecl> --> FUNCTION
//				LEX_LEFT_PARENTHESIS
//				<destination>,
//				LEX_RIGHT_PARENTHESIS <statements> LEX_END
struct symbol *fdecl()
{
	FETCH_OR_QUIT(LEX_FUNCTION)
	FETCH_OR_ERROR(LEX_LEFTHESIS);
	struct symbol *s = symbol_new(SYMBOL_FDECL);
	s->index = repeated(SYMBOL_DESTINATION, &destination);
	FETCH_OR_ERROR(LEX_RIGHTHESIS)
	s->value = statements();
	DEBUGPRINT("fdecl: %d statements\n", s->value->list->length);
	DEBUGPRINT("lookahead=%s\n", NUM_TO_STRING(lexemes, LOOKAHEAD));
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
//		p->index = symbol_new(SYMBOL_NIL);
		return e;
	}
}

// <table> --> LEX_LEFTSQUARE <element>, LEX_RIGHTSQUARE
struct symbol *table() {
	//return list(SYMBOL_TABLE, LEX_LEFTSQUARE, LEX_RIGHTSQUARE, LEX_COLON);
	FETCH_OR_QUIT(LEX_LEFTSQUARE);
	struct symbol *s = repeated(SYMBOL_TABLE, &element);
	FETCH_OR_ERROR(LEX_RIGHTSQUARE);
	return s;
}

struct symbol *integer()
{
	//	DEBUGPRINT("integer\n");
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

struct symbol *floater()
{
	//	DEBUGPRINT("floater\n");
	struct token *t = fetch(LEX_INTEGER);
	if (!t)
		return NULL;
	FETCH_OR_QUIT(LEX_PERIOD);
	struct token *u = fetch(LEX_INTEGER);

	uint32_t decimal = u->number;
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

//  <atom> -->  LEX_IDENTIFIER | <float> | <integer> | <boolean> | <table> | <fdecl>
struct symbol *atom()
{
	return one_of(&variable, &string, &floater, &integer, &boolean, &table, &fdecl, NULL);
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

// <call> --> LEX_LEFT_PARENTHESIS <source>, LEX_RIGHT_PARENTHESIS
struct symbol *call()
{
	struct symbol *s = symbol_new(SYMBOL_FCALL);
	FETCH_OR_QUIT(LEX_LEFTHESIS);
	s->index = repeated(SYMBOL_SOURCE, &expression); // arguments
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
	if ((e = symbol_fetch(SYMBOL_EXPRESSION, LEX_MINUS, LEX_NOT, NULL)))
		return symbol_add(e, exp3());
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

/*	<ifthenelse> --> IF <expression>
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
	s->index = expression();
	s->value = statements();
	FETCH_OR_ERROR(LEX_END);
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
	FETCH_OR_QUIT(LEX_RETURN);
	return repeated(SYMBOL_RETURN, &expression); // return values
}

// <statements> --> ( <assignment> | <fcall> | <ifthenelse> | <loop> | <rejoinder> ) *
struct symbol *statements()
{
	struct symbol *s = symbol_new(SYMBOL_STATEMENTS);
	struct symbol *t;
	while ((t = one_of(&assignment, &fcall, &ifthenelse, &loop, &rejoinder, NULL)))
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

void generate_code(struct byte_array *code, struct symbol *root);
struct byte_array *build_file(const struct byte_array* filename);

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

void generate_items(struct byte_array *code, const struct symbol* root)
{
	const struct array *items = root->list;
	uint32_t num_items = items->length;
	
	for (int i=0; i<num_items; i++) {
		struct symbol *item = array_get(items, i);
		generate_code(code, item);
	}
}

void generate_items_then_op(struct byte_array *code, enum Opcode opcode, const struct symbol* root)
{
	generate_items(code, root);	
	generate_step(code, 1, opcode);
	serial_encode_int(code, 0, root->list->length);
}

void generate_statements(struct byte_array *code, struct symbol *root)
{
	if (!root)
		return;
	generate_items(code, root);
}

void generate_math(struct byte_array *code, struct symbol *root)
{
	enum Lexeme lexeme = root->token->lexeme;
	generate_statements(code, root);
	enum Opcode op;
	switch (lexeme) {
		case LEX_PLUS:			op = VM_ADD;			break;
		case LEX_MINUS:			op = VM_SUB;			break;
		case LEX_TIMES:			op = VM_MUL;			break;
		case LEX_DIVIDE:		op = VM_DIV;			break;
		case LEX_AND:			op = VM_AND;			break;
		case LEX_OR:			op = VM_OR;				break;
		case LEX_NOT:			op = VM_NOT;			break;
		case LEX_SAME:			op = VM_EQU;			break;
		case LEX_DIFFERENT:		op = VM_NEQ;			break;
		case LEX_GREATER:		op = VM_GT;				break;
		case LEX_LESSER:		op = VM_LT;				break;
		default:	exit_message("bad math lexeme");	break;
	}
	generate_step(code, 1, op);
}

static void generate_jump(struct byte_array *code, uint32_t offset)
{
	generate_step(code, 2, VM_JMP, offset);
}

void generate_ifthenelse(struct byte_array *code, struct symbol *root)
{
	struct array *gotos = array_new(); // for skipping to end of elseifs, if one is true

	for (int i=0; i<root->list->length; i+=2) {

		// then
		struct symbol *thn = array_get(root->list, i+1);
		assert_message(thn->nonterminal == SYMBOL_STATEMENTS, "branch syntax error");
		struct byte_array *thn_code = byte_array_new();
		generate_code(thn_code, thn);
		generate_jump(thn_code, 0); // jump to end

		// if
		struct symbol *iff = array_get(root->list, i);
		generate_code(code, iff);
		generate_step(code, 2, VM_IF, thn_code->size);
		byte_array_append(code, thn_code);
		array_add(gotos, (void*)(VOID_INT)(code->size-1));

		// else
		if (root->list->length > i+2) {
			struct symbol *els = array_get(root->list, i+2);
			if (els->nonterminal == SYMBOL_STATEMENTS) {
				assert_message(root->list->length == i+3, "else should be the last branch");
				generate_code(code, els);
				break;
			}
		}
	}

	// go back and fill in where to jump to the end of if-then-elseif-else when done
	for (int j=0; j<gotos->length; j++) {
		VOID_INT g = (VOID_INT)array_get(gotos, j);
		code->data[g] = code->size - g;
	}
}

void generate_loop(struct byte_array *code, struct symbol *root)
{
	struct byte_array *ifa = byte_array_new();
	generate_code(ifa, root->index);

	struct byte_array *b = byte_array_new();
	generate_code(b, root->value);

	struct byte_array *thn = byte_array_new();
	generate_step(thn, 2, VM_IF, b->size + 2);

	struct byte_array *while_a_do_b = byte_array_concatenate(4, code, ifa, thn, b);
	byte_array_append(code, while_a_do_b);
	uint8_t loop_length = ifa->size + thn->size + b->size;
	generate_jump(code, -loop_length-1); // todo: handle large jumps
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

void generate_boolean(struct byte_array *code, struct symbol *root) {
	uint32_t value = 0;
	switch (root->token->lexeme) {
		case		LEX_TRUE:	value = 1;						break;
		case		LEX_FALSE:	value = 0;						break;
		default:	exit_message("bad boolean value");			return;
	}
	generate_step(code, 1, VM_BOOL);
	serial_encode_int(code, 0, value);
}

void generate_string(struct byte_array *code, struct symbol *root) {
	generate_step(code, 1, VM_STR);
	serial_encode_string(code, 0, root->token->string);
}

void generate_source(struct byte_array *code, struct symbol *root)
{
	generate_items_then_op(code, VM_SRC, root);
}

void generate_destination(struct byte_array *code, struct symbol *root)
{
	//	generate_items_then_op(code, VM_DST, root);
	generate_items(code, root);	
	generate_step(code, 1, VM_DST);

/*	struct array *dst = root->list;
	uint32_t num_dst = dst->length;

	for (int i=0; i<num_dst; i++) {
		struct symbol *pair = array_get(dst, i);
		assert_message(pair->nonterminal == SYMBOL_PAIR, "not a pair");
		generate_code(code, pair->value);
	}

	generate_step(code, 2, VM_DST, num_dst);

	for (int i=0; i<num_dst; i++) {
		struct symbol *pair = array_get(dst, i);
//		serial_encode_string(code, 0, pair->index->token->string);
		generate_code(code, pair->index);
	}*/
}

void generate_assignment(struct byte_array *code, struct symbol *root)
{
	//	generate_items_then_op(code, VM_SRC, root);
	generate_code(code, root->value);
	generate_code(code, root->index);
}

void generate_fdecl(struct byte_array *code, struct symbol *root)
{
	struct byte_array *f = byte_array_new();
	generate_code(f, root->index);
	generate_code(f, root->value);
	generate_step(code, 1, VM_FNC);
	serial_encode_string(code, 0, f);
}

void generate_variable(struct byte_array *code, struct symbol *root)
{
	generate_step(code, 1, root->lhs ? VM_SET : VM_VAR);
	serial_encode_string(code, 0, root->token->string);
}

void generate_pair(struct byte_array *code, struct symbol *root)
{
	generate_code(code, root->index);
	generate_code(code, root->value);
	generate_step(code, 2, VM_MAP, 1);
}

void generate_list(struct byte_array *code, struct symbol *root)
{
	generate_items_then_op(code, VM_LST, root);
}

void generate_member(struct byte_array *code, struct symbol *root)
{
	generate_code(code, root->index); // array_get(root->branches, INDEX));
	generate_code(code, root->value); // array_get(root->branches, ITERABLE));

	enum Opcode op = root->lhs ? VM_PUT : VM_GET;
	generate_step(code, 1, op);
}

void generate_fcall(struct byte_array *code, struct symbol *root)
{
	generate_code(code, root->index); // arguments

	if (root->value->nonterminal == SYMBOL_MEMBER) {
		generate_code(code, root->value->index);
		generate_code(code, root->value->value);
		generate_step(code, 1, VM_MET);
	} else {
		generate_code(code, root->value); // function
		generate_step(code, 1, VM_CAL);		
	}
	//	generate_destination(code, root);
}

void generate_return(struct byte_array *code, struct symbol *root)
{
	generate_items_then_op(code, VM_RET, root);
}

void generate_nil(struct byte_array *code)
{
	generate_step(code, 1, VM_NIL);
}

void generate_code(struct byte_array *code, struct symbol *root)
{
	if (root==0)
		return;

	//DEBUGPRINT("generate_code %s\n", nonterminals[root->nonterminal]);
	switch(root->nonterminal) {
		case SYMBOL_STATEMENTS:		generate_statements(code, root);	break;
		case SYMBOL_ASSIGNMENT:		generate_assignment(code, root);	break;
		case SYMBOL_SOURCE:			generate_source(code, root);		break;
		case SYMBOL_DESTINATION:	generate_destination(code, root);	break;
		case SYMBOL_EXPRESSION:		generate_math(code, root);			break;
		case SYMBOL_NIL:			generate_nil(code);					break;
		case SYMBOL_INTEGER:		generate_integer(code, root);		break;
		case SYMBOL_BOOLEAN:		generate_boolean(code, root);		break;
		case SYMBOL_FLOAT:			generate_float(code, root);			break;
		case SYMBOL_STRING:			generate_string(code, root);		break;
		case SYMBOL_VARIABLE:		generate_variable(code, root);		break;
		case SYMBOL_IF_THEN_ELSE:	generate_ifthenelse(code, root);	break;
		case SYMBOL_FCALL:			generate_fcall(code, root);			break;
		case SYMBOL_MEMBER:			generate_member(code, root);		break;
		case SYMBOL_FDECL:			generate_fdecl(code, root);			break;
		case SYMBOL_PAIR:			generate_pair(code, root);			break;
		case SYMBOL_TABLE:			generate_list(code, root);			break;
		case SYMBOL_LOOP:			generate_loop(code, root);			break;
		case SYMBOL_RETURN:			generate_return(code, root);		break;
		default:
			exit_message(ERROR_TOKEN);
			return;
	}
}

struct byte_array *program_from_code(struct byte_array *code, struct symbol *root)
{
	DEBUGPRINT("generate:\n");
	generate_code(code, root);
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
	DEBUGPRINT("lex:\n");
	struct array* list = lex(input_copy);
	struct symbol *tree = parse(list, 0);

	struct byte_array *code = byte_array_new();
	return program_from_code(code, tree);
}

struct byte_array *build_file(const struct byte_array* filename)
{
	struct byte_array *input = read_file(filename);
	return build_string(input);
}

// main: read file, build, run /////////////////////////////////////////////

#define ERROR_USAGE	"usage: main <script filename>"

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