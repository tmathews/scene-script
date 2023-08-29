package script

import (
	"fmt"
	"strconv"
	"unicode"
)

type TokenType uint8

var TokenTypeStrings = map[TokenType]string{
	TokInvalid: "Invalid",
	TokScript:  "Script",
	TokNumber:  "Number",
	TokString:  "String",
	TokWord:    "Word",
	TokMinus:   "Minus",
	TokNot:     "Not",
	TokDollar:  "Dollar",
	TokAnd:     "And",
	TokOr:      "Or",
	TokComma:   "Comma",
	TokColon:   "Colon",
	TokLParen:  "LParen",
	TokRParen:  "RParen",
	TokIndent:  "Indent",
}

func (t TokenType) String() string {
	return TokenTypeStrings[t]
}

const (
	TokInvalid TokenType = iota
	TokScript
	TokNumber
	TokString
	TokWord
	TokMinus
	TokNot
	TokDollar
	TokAnd
	TokOr
	TokComma
	TokColon
	TokLParen
	TokRParen
	TokIndent
)

type Token struct {
	Type    TokenType
	Literal string
}

func (t Token) String() string {
	return fmt.Sprintf("[%s]: %s", t.Type.String(), t.Literal)
}

func Lexer(buf []rune) ([]Token, error) {
	var xs []Token
	var inString, inComment bool
	var builder []rune
	var lastChar rune
	for _, char := range buf {
		if inComment {
			if char == '\n' {
				inComment = false
			}
			continue
		}
		if !inString && char == '#' {
			inComment = true
			continue
		}
		if inString || len(builder) > 0 {
			var clear, skip bool
			if inString {
				if char == '`' && lastChar != '\\' {
					inString = false
					xs = append(xs, Token{
						Type:    TokString,
						Literal: string(builder),
					})
					clear = true
					skip = true
				}
			} else if !IsTokWordChar(char) {
				xs = append(xs, TokenFromString(string(builder)))
				clear = true
			}
			if clear {
				builder = []rune{}
			}
			if skip {
				lastChar = char
				continue
			}
		}
		tt := TokenTypeFromChar(char)
		if !inString && tt != TokInvalid {
			xs = append(xs, Token{
				Type:    tt,
				Literal: string([]rune{char}),
			})
		} else if inString || IsTokWordChar(char) {
			builder = append(builder, char)
		} else if char == '`' {
			inString = true
		}
		lastChar = char
	}
	if len(builder) > 0 {
		xs = append(xs, TokenFromString(string(builder)))
	}
	return xs, nil
}

func TokenFromString(str string) Token {
	// XXX this function could be improved with just one loop
	var t Token
	t.Literal = str
	if str == "and" {
		t.Type = TokAnd
	} else if str == "or" {
		t.Type = TokOr
	} else if _, err := strconv.ParseFloat(str, 64); err == nil {
		t.Type = TokNumber
	} else {
		for _, char := range str {
			if !IsTokWordChar(char) {
				return t
			}
		}
		t.Type = TokWord
	}
	return t
}

func TokenTypeFromChar(char rune) TokenType {
	switch char {
	case '!':
		return TokNot
	case '-':
		return TokMinus
	case '$':
		return TokDollar
	case ',':
		return TokComma
	case ':':
		return TokColon
	case '(':
		return TokLParen
	case ')':
		return TokRParen
	case '\t':
		return TokIndent
	}
	return TokInvalid
}

func IsTokWordChar(char rune) bool {
	return char == '.' || unicode.IsLetter(char) || unicode.IsNumber(char)
}

func isPrefixToken(t Token) bool {
	return t.Type == TokNot ||
		t.Type == TokMinus ||
		t.Type == TokDollar
}

func isInfixToken(t Token) bool {
	switch t.Type {
	case TokLParen, TokMinus, TokAnd, TokOr:
		return true
	}
	return false
}

func isExpToken(t Token) bool {
	kw := LiteralToKeyword(t.Literal)
	if kw != KeyInvalid {
		if kw == KeyTrue || kw == KeyFalse {
			return true
		}
		return false
	}
	switch t.Type {
	case TokLParen,
		TokWord,
		TokString,
		TokNumber:
		return true
	}
	return isInfixToken(t) || isPrefixToken(t)
}

const (
	PrecLowest uint = iota
	PrecInfix
	PrecSum
	PrecPrefix
	PrecCall
)

func getTokenPrecedence(t Token) uint {
	switch t.Type {
	case TokAnd, TokOr:
		return PrecInfix
	case TokMinus:
		return PrecSum
	case TokLParen:
		return PrecCall
	}
	return PrecLowest
}
