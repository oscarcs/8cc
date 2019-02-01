// Copyright 2012 Rui Ueyama. Released under the MIT license.

/*
 * Tokenizer
 *
 * This is a translation phase after the phase 1 and 2 in file.c.
 * In this phase, the source code is decomposed into preprocessing tokens.
 *
 * Each comment is treated as if it were a space character.
 * Space characters are removed, but the presence of the characters is
 * recorded to the token that immediately follows the spaces as a boolean flag.
 * Newlines are converted to newline tokens.
 *
 * Note that the pp-token is different from the regular token.
 * A keyword, such as "if", is just an identifier at this stage.
 * The definition of the pp-token is usually more relaxed than
 * the regular one. For example, ".32e." is a valid pp-number.
 * Pp-tokens are converted to regular tokens by the C preprocesor
 * (and invalid tokens are rejected by that).
 * Some tokens are removed by the preprocessor (e.g. newline).
 * For more information about pp-tokens, see C11 6.4 "Lexical Elements".
 */

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include "8cc.h"

// EMPTY_VECTOR is a macro defined in the 8cc header file.
static Vector *buffers = &EMPTY_VECTOR;
static Token *space_token = &(Token){ TSPACE };
static Token *newline_token = &(Token){ TNEWLINE };
static Token *eof_token = &(Token){ TEOF };

typedef struct {
    int line;
    int column;
} Pos;

static Pos pos;

static char *pos_string(Pos *p) {
    File *f = current_file();
    return format("%s:%d:%d", f ? f->name : "(unknown)", p->line, p->column);
}

#define errorp(p, ...) errorf(__FILE__ ":" STR(__LINE__), pos_string(&p), __VA_ARGS__)
#define warnp(p, ...)  warnf(__FILE__ ":" STR(__LINE__), pos_string(&p), __VA_ARGS__)

static void skip_block_comment(void);

void lex_init(char *filename) {

    // A vector is a resizeable container of pointers. Here, we create a 
    // new empty vector and push it onto the 'buffers' vector.
    vec_push(buffers, make_vector());

    // If the filename equals "-"...
    if (!strcmp(filename, "-")) {
        // Push stdin onto the files vector.
        stream_push(make_file(stdin, "-"));
        return;
    }

    // Otherwise, open the specified file.
    FILE *fp = fopen(filename, "r");
    if (!fp)
        error("Cannot open %s: %s", filename, strerror(errno));

    // Push the file onto the vector.
    stream_push(make_file(fp, filename));
}

// Get the position in the current file, including a delta from the current column.
static Pos get_pos(int delta) {
    File *f = current_file();
    return (Pos){ f->line, f->column + delta };
}

static void mark() {
    pos = get_pos(0);
}

// Make a token struct.
static Token *make_token(Token *tmpl) {
    // Allocate memory for the token.
    Token *r = malloc(sizeof(Token));

    // Set the token's contents to be like the provided template.
    *r = *tmpl;

    // Hideset is used by the preprocessor for macro expansion.
    r->hideset = NULL;

    // Set file-related properties on the token. 
    File *f = current_file();
    r->file = f;
    r->line = pos.line;
    r->column = pos.column;

    // Index of the token within the file.
    r->count = f->ntok++;
    return r;
}

// Make an identifier token.
static Token *make_ident(char *p) {
    return make_token(&(Token){ TIDENT, .sval = p });
}

// Make a string token.
static Token *make_strtok(char *s, int len, int enc) {
    return make_token(&(Token){ TSTRING, .sval = s, .slen = len, .enc = enc });
}

// Make a keyword token.
static Token *make_keyword(int id) {
    return make_token(&(Token){ TKEYWORD, .id = id });
}

// Make a number token.
static Token *make_number(char *s) {
    return make_token(&(Token){ TNUMBER, .sval = s });
}

// Make an invalid token.
static Token *make_invalid(char c) {
    return make_token(&(Token){ TINVALID, .c = c });
}

// Make a char token.
static Token *make_char(int c, int enc) {
    return make_token(&(Token){ TCHAR, .c = c, .enc = enc });
}

// Check if a given character is a whitespace character.
static bool iswhitespace(int c) {
    // ' ' is obviously a space.
    // '\t' is the tab character.
    // '\f' is the form feed character, which was used in ancient times as a
    // page break character.
    // '\v' is the vertical tab character, which moved the printer vertically.
    return c == ' ' || c == '\t' || c == '\f' || c == '\v';
}

// Look at the next char and then immediately unread it.
static int peek() {
    int r = readc();
    unreadc(r);
    return r;
}

// Check that the next character conforms to expectation.
static bool next(int expect) {
    int c = readc();
    if (c == expect)
        return true;
    unreadc(c);
    return false;
}

// Skip a line of input.
static void skip_line() {
    // Continue reading input until we encounter EOF or a newline.
    for (;;) {
        int c = readc();
        if (c == EOF)
            return;
        if (c == '\n') {
            unreadc(c);
            return;
        }
    }
}

// Skip space characters, see the function below.
static bool do_skip_space() {
    int c = readc();
    if (c == EOF)
        return false;

    // Skip a space character.
    if (iswhitespace(c))
        return true;

    // Skip comments.
    if (c == '/') {
        // Block comments start with /*
        if (next('*')) {
            skip_block_comment();
            return true;
        }
        // Line comments start with //
        if (next('/')) {
            skip_line();
            return true;
        }
    }
    unreadc(c);
    return false;
}

// Skips spaces including comments.
// Returns true if at least one space is skipped.
static bool skip_space() {
    if (!do_skip_space())
        return false;
    
    // Continue skipping spaces until there are no more spaces to skip.
    while (do_skip_space());
    return true;
}

// Skip reading a character literal (like '\t').
static void skip_char() {
    // Escaped characters have a \ that needs to be skipped.
    if (readc() == '\\')
        readc();
    int c = readc();

    // Skip characters until a ' or EOF is reached.
    while (c != EOF && c != '\'')
        c = readc();
}

// Skip a string (like "hello").
static void skip_string() {
    // Continue reading until " or EOF is reached.
    for (int c = readc(); c != EOF && c != '"'; c = readc())
        if (c == '\\')
            readc();
}

// Skips a block of code excluded from input by #if, #ifdef and the like.
// C11 6.10 says that code within #if and #endif needs to be a sequence of
// valid tokens even if skipped. However, in reality, most compilers don't
// tokenize nor validate contents. We don't do that, too.
// This function is to skip code until matching #endif as fast as we can.
void skip_cond_incl() {
    int nest = 0;
    for (;;) {
        // 'bol' stands for 'beginning of line'.
        bool bol = (current_file()->column == 1);
        skip_space();
        int c = readc();

        // If we reached the end of the file, we're done.
        if (c == EOF)
            return;

        // Skip char literals.
        if (c == '\'') {
            skip_char();
            continue;
        }

        // Skip string literals.
        if (c == '\"') {
            skip_string();
            continue;
        }

        // Detect whether we've found a preprocessor directive, which starts 
        // with # on the first column.
        if (c != '#' || !bol)
            continue;

        // Lex a token.
        int column = current_file()->column - 1;
        Token *tok = lex();
        
        // Check that the current token is a identifier. 
        if (tok->kind != TIDENT)
            continue;

        // Check for #else, #elif, and #endif directives with a nesting level 
        // of 0. One of these directives at a nesting level of 0 is an error in
        // the input, so we need to try something else:
        if (!nest && (is_ident(tok, "else") || is_ident(tok, "elif") || is_ident(tok, "endif"))) {
            // Unget the token we just lexed.
            unget_token(tok);

            // Create a hash token and then unget it, putting it in the buffer.
            Token *hash = make_keyword('#');
            hash->bol = true;
            hash->column = column;
            unget_token(hash);

            return;
        }

        // Check for #if, #idef, and #ifndef directives. We increase the nesting
        // level in this case.
        if (is_ident(tok, "if") || is_ident(tok, "ifdef") || is_ident(tok, "ifndef"))
            nest++;
        else if (nest && is_ident(tok, "endif"))
            nest--;
        
        // Skip the rest of the the line.
        skip_line();
    }
}

// Reads a number literal. Lexer's grammar on numbers is not strict.
// Integers and floating point numbers and different base numbers are not distinguished.
static Token *read_number(char c) {
    // Create a new buffer to store characters in.
    Buffer *b = make_buffer();

    // Write the first character.
    buf_write(b, c);
    char last = c;

    for (;;) {
        int c = readc();

        // strchr() searches through a string (the first argument) until it finds
        // an instance of the character provided as the second argument.
        // The two calls to strchr() here look for e/E/p/P, which are exponent
        // signs, and +/-, which is the sign.
        bool flonum = strchr("eEpP", last) && strchr("+-", c);

        // Characters in a numeric literal have to consist of digits, alphabetic
        // characters, periods, and the characters above.
        if (!isdigit(c) && !isalpha(c) && c != '.' && !flonum) {
            // If we encounter a character that can't be in a numeric literal,
            // we unread it, close the buffer with a \0, and return the token.
            unreadc(c);
            buf_write(b, '\0');
            return make_number(buf_body(b));
        }

        // Write a character into the buffer.
        buf_write(b, c);
        last = c;
    }
}

// Octal numbers, unsurprisingly, are only allowed to contain the digits 0-7.
static bool nextoct() {
    int c = peek();
    return '0' <= c && c <= '7';
}

// Reads an octal escape sequence.
static int read_octal_char(int c) {
    int r = c - '0';
    if (!nextoct())
        return r;
    r = (r << 3) | (readc() - '0');
    if (!nextoct())
        return r;
    return (r << 3) | (readc() - '0');
}

// Reads a \x escape sequence.
static int read_hex_char() {
    // Get the position two positions previous to the current one, i.e. the
    // position of the start of the escape sequence.
    Pos p = get_pos(-2);
    int c = readc();

    // isxdigit() is a C standard library function that checks if a character
    // is a hexadecimal digit.
    if (!isxdigit(c))
        errorp(p, "\\x is not followed by a hexadecimal character: %c", c);
    
    // Now we actually parse the value. r records the current value. At each
    // iteration, we parse the next character, converting the value of the
    // char in ASCII to a corresponding value and then shifting it the correct
    // amount:
    int r = 0;
    for (;; c = readc()) {
        switch (c) {
        case '0' ... '9': r = (r << 4) | (c - '0'); continue;
        case 'a' ... 'f': r = (r << 4) | (c - 'a' + 10); continue;
        case 'A' ... 'F': r = (r << 4) | (c - 'A' + 10); continue;
        default: unreadc(c); return r;
        }
    }
}

static bool is_valid_ucn(unsigned int c) {
    // C11 6.4.3p2: U+D800 to U+DFFF are reserved for surrogate pairs.
    // A codepoint within the range cannot be a valid character.
    if (0xD800 <= c && c <= 0xDFFF)
        return false;
    // It's not allowed to encode ASCII characters using \U or \u.
    // Some characters not in the basic character set (C11 5.2.1p3)
    // are allowed as exceptions.
    return 0xA0 <= c || c == '$' || c == '@' || c == '`';
}

// Reads \u or \U escape sequences. len is 4 or 8, respectively.
static int read_universal_char(int len) {
    Pos p = get_pos(-2);
    unsigned int r = 0;
    for (int i = 0; i < len; i++) {
        char c = readc();

        // Functions in exactly the same way as the switch statement above.
        switch (c) {
        case '0' ... '9': r = (r << 4) | (c - '0'); continue;
        case 'a' ... 'f': r = (r << 4) | (c - 'a' + 10); continue;
        case 'A' ... 'F': r = (r << 4) | (c - 'A' + 10); continue;
        default: errorp(p, "invalid universal character: %c", c);
        }
    }
    // Use the function defined above to check that this is a valid unicode
    // codepoint.
    if (!is_valid_ucn(r))
        errorp(p, "invalid universal character: \\%c%0*x", (len == 4) ? 'u' : 'U', len, r);
    return r;
}

// Perhaps the most interesting function in the entire compiler; the original 
// comment below does a good job of explaining what is happening. It's worth 
// pointing out, though, that this is not actually a 'tautology' as such; the
// information is present in the binary used to compile this compiler.
// I wonder if a more general version of this principle could be useful for
// some other purpose.
static int read_escaped_char() {
    Pos p = get_pos(-1);
    int c = readc();
    // This switch-cases is an interesting example of magical aspects
    // of self-hosting compilers. Here, we teach the compiler about
    // escaped sequences using escaped sequences themselves.
    // This is a tautology. The information about their real character
    // codes is not present in the source code but propagated from
    // a compiler compiling the source code.
    // See "Reflections on Trusting Trust" by Ken Thompson for more info.
    // http://cm.bell-labs.com/who/ken/trust.html
    switch (c) {
    case '\'': case '"': case '?': case '\\':
        return c;
    case 'a': return '\a';
    case 'b': return '\b';
    case 'f': return '\f';
    case 'n': return '\n';
    case 'r': return '\r';
    case 't': return '\t';
    case 'v': return '\v';
    case 'e': return '\033';  // '\e' is GNU extension
    case 'x': return read_hex_char();
    case 'u': return read_universal_char(4);
    case 'U': return read_universal_char(8);
    case '0' ... '7': return read_octal_char(c);
    }
    warnp(p, "unknown escape character: \\%c", c);
    return c;
}

// Read a character literal.
static Token *read_char(int enc) {
    // Read a normal character.
    int c = readc();

    // If the character is a \, we need to treat it as an escape character.
    int r = (c == '\\') ? read_escaped_char() : c;
    c = readc();

    // The last character needs to be the quote mark.
    if (c != '\'')
        errorp(pos, "unterminated char");
    
    // ENC_NONE is a character without any special encoding, so we can use the
    // normal C datatype 'char'.
    if (enc == ENC_NONE)
        return make_char((char)r, enc);

    // Otherwise return a character literal with a certain encoding.
    return make_char(r, enc);
}

// Reads a string literal.
static Token *read_string(int enc) {
    // Make a buffer to keep the string in.
    Buffer *b = make_buffer();
    for (;;) {
        int c = readc();
        if (c == EOF)
            errorp(pos, "unterminated string");

        // Keep reading until we encounter a ", indicating that the string is
        // finished.
        if (c == '"')
            break;
        
        // If the current character isn't an escape character, we're in the clear;
        // we can read the next character.
        if (c != '\\') {
            buf_write(b, c);
            continue;
        }
        // Check to see if the escape character is unicode.
        bool isucs = (peek() == 'u' || peek() == 'U');
        c = read_escaped_char();

        // If it is unicode, write utf8.
        if (isucs) {
            write_utf8(b, c);
            continue;
        }
        buf_write(b, c);
    }
    // End the string.
    buf_write(b, '\0');
    
    // Make a token from the buffer.
    return make_strtok(buf_body(b), buf_len(b), enc);
}

static Token *read_ident(char c) {
    Buffer *b = make_buffer();
    buf_write(b, c);
    for (;;) {
        c = readc();
        if (isalnum(c) || (c & 0x80) || c == '_' || c == '$') {
            buf_write(b, c);
            continue;
        }
        // C11 6.4.2.1: \u or \U characters (universal-character-name)
        // are allowed to be part of identifiers.
        if (c == '\\' && (peek() == 'u' || peek() == 'U')) {
            write_utf8(b, read_escaped_char());
            continue;
        }
        unreadc(c);
        buf_write(b, '\0');
        return make_ident(buf_body(b));
    }
}

static void skip_block_comment() {
    Pos p = get_pos(-2);
    bool maybe_end = false;
    for (;;) {
        int c = readc();
        if (c == EOF)
            errorp(p, "premature end of block comment");
        if (c == '/' && maybe_end)
            return;
        maybe_end = (c == '*');
    }
}

// Reads a digraph starting with '%'. Digraphs are alternative spellings
// for some punctuation characters. They are useless in ASCII.
// We implement this just for the standard compliance.
// See C11 6.4.6p3 for the spec.
static Token *read_hash_digraph() {
    if (next('>'))
        return make_keyword('}');
    if (next(':')) {
        if (next('%')) {
            if (next(':'))
                return make_keyword(KHASHHASH);
            unreadc('%');
        }
        return make_keyword('#');
    }
    return NULL;
}

static Token *read_rep(char expect, int t1, int els) {
    return make_keyword(next(expect) ? t1 : els);
}

static Token *read_rep2(char expect1, int t1, char expect2, int t2, char els) {
    if (next(expect1))
        return make_keyword(t1);
    return make_keyword(next(expect2) ? t2 : els);
}

static Token *do_read_token() {
    if (skip_space())
        return space_token;
    mark();
    int c = readc();
    switch (c) {
    case '\n': return newline_token;
    case ':': return make_keyword(next('>') ? ']' : ':');
    case '#': return make_keyword(next('#') ? KHASHHASH : '#');
    case '+': return read_rep2('+', OP_INC, '=', OP_A_ADD, '+');
    case '*': return read_rep('=', OP_A_MUL, '*');
    case '=': return read_rep('=', OP_EQ, '=');
    case '!': return read_rep('=', OP_NE, '!');
    case '&': return read_rep2('&', OP_LOGAND, '=', OP_A_AND, '&');
    case '|': return read_rep2('|', OP_LOGOR, '=', OP_A_OR, '|');
    case '^': return read_rep('=', OP_A_XOR, '^');
    case '"': return read_string(ENC_NONE);
    case '\'': return read_char(ENC_NONE);
    case '/': return make_keyword(next('=') ? OP_A_DIV : '/');
    case 'a' ... 't': case 'v' ... 'z': case 'A' ... 'K':
    case 'M' ... 'T': case 'V' ... 'Z': case '_': case '$':
    case 0x80 ... 0xFD:
        return read_ident(c);
    case '0' ... '9':
        return read_number(c);
    case 'L': case 'U': {
        // Wide/char32_t character/string literal
        int enc = (c == 'L') ? ENC_WCHAR : ENC_CHAR32;
        if (next('"'))  return read_string(enc);
        if (next('\'')) return read_char(enc);
        return read_ident(c);
    }
    case 'u':
        if (next('"')) return read_string(ENC_CHAR16);
        if (next('\'')) return read_char(ENC_CHAR16);
        // C11 6.4.5: UTF-8 string literal
        if (next('8')) {
            if (next('"'))
                return read_string(ENC_UTF8);
            unreadc('8');
        }
        return read_ident(c);
    case '.':
        if (isdigit(peek()))
            return read_number(c);
        if (next('.')) {
            if (next('.'))
                return make_keyword(KELLIPSIS);
            return make_ident("..");
        }
        return make_keyword('.');
    case '(': case ')': case ',': case ';': case '[': case ']': case '{':
    case '}': case '?': case '~':
        return make_keyword(c);
    case '-':
        if (next('-')) return make_keyword(OP_DEC);
        if (next('>')) return make_keyword(OP_ARROW);
        if (next('=')) return make_keyword(OP_A_SUB);
        return make_keyword('-');
    case '<':
        if (next('<')) return read_rep('=', OP_A_SAL, OP_SAL);
        if (next('=')) return make_keyword(OP_LE);
        if (next(':')) return make_keyword('[');
        if (next('%')) return make_keyword('{');
        return make_keyword('<');
    case '>':
        if (next('=')) return make_keyword(OP_GE);
        if (next('>')) return read_rep('=', OP_A_SAR, OP_SAR);
        return make_keyword('>');
    case '%': {
        Token *tok = read_hash_digraph();
        if (tok)
            return tok;
        return read_rep('=', OP_A_MOD, '%');
    }
    case EOF:
        return eof_token;
    default: return make_invalid(c);
    }
}

static bool buffer_empty() {
    return vec_len(buffers) == 1 && vec_len(vec_head(buffers)) == 0;
}

// Reads a header file name for #include.
//
// Filenames after #include need a special tokenization treatment.
// A filename string may be quoted by < and > instead of "".
// Even if it's quoted by "", it's still different from a regular string token.
// For example, \ in this context is not interpreted as a quote.
// Thus, we cannot use lex() to read a filename.
//
// That the C preprocessor requires a special lexer behavior only for
// #include is a violation of layering. Ideally, the lexer should be
// agnostic about higher layers status. But we need this for the C grammar.
char *read_header_file_name(bool *std) {
    if (!buffer_empty())
        return NULL;
    skip_space();
    Pos p = get_pos(0);
    char close;
    if (next('"')) {
        *std = false;
        close = '"';
    } else if (next('<')) {
        *std = true;
        close = '>';
    } else {
        return NULL;
    }
    Buffer *b = make_buffer();
    while (!next(close)) {
        int c = readc();
        if (c == EOF || c == '\n')
            errorp(p, "premature end of header name");
        buf_write(b, c);
    }
    if (buf_len(b) == 0)
        errorp(p, "header name should not be empty");
    buf_write(b, '\0');
    return buf_body(b);
}

bool is_keyword(Token *tok, int c) {
    return (tok->kind == TKEYWORD) && (tok->id == c);
}

// Temporarily switches the input token stream to given list of tokens,
// so that you can get the tokens as return values of lex() again.
// After the tokens are exhausted, EOF is returned from lex() until
// "unstash" is called to restore the original state.
void token_buffer_stash(Vector *buf) {
    vec_push(buffers, buf);
}

void token_buffer_unstash() {
    vec_pop(buffers);
}

void unget_token(Token *tok) {
    if (tok->kind == TEOF)
        return;
    Vector *buf = vec_tail(buffers);
    vec_push(buf, tok);
}

// Reads a token from a given string.
// This function temporarily switches the main input stream to
// a given string and reads one token.
Token *lex_string(char *s) {
    stream_stash(make_file_string(s));
    Token *r = do_read_token();
    next('\n');
    Pos p = get_pos(0);
    if (peek() != EOF)
        errorp(p, "unconsumed input: %s", s);
    stream_unstash();
    return r;
}

// The main lexer function.
Token *lex() {
    Vector *buf = vec_tail(buffers);
    if (vec_len(buf) > 0)
        return vec_pop(buf);
    if (vec_len(buffers) > 1)
        return eof_token;
    bool bol = (current_file()->column == 1);
    Token *tok = do_read_token();
    while (tok->kind == TSPACE) {
        tok = do_read_token();
        tok->space = true;
    }
    tok->bol = bol;
    return tok;
}
