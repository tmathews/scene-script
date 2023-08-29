package script

import (
	"errors"
	"fmt"
)

var ErrInvalidExpressionToken = errors.New("Ran into invalid expression token")

type Keyword uint8

const (
	KeyInvalid Keyword = iota
	KeyScript
	KeyIf
	KeyElif
	KeyElse
	KeyEnd
	KeyRun
	KeyEq
	KeyTrue
	KeyFalse
)

type StatementType uint8

const (
	StmtInvalid StatementType = iota
	StmtExpression
	StmtEnd
	StmtRun
	StmtIf
	StmtElif
	StmtElse
)

type ExpressionType uint8

const (
	ExpEmpty ExpressionType = iota
	ExpBool
	ExpNumber
	ExpString
	ExpWord
	ExpPrefix
	ExpInfix
	ExpCall
)

type Script struct {
	Name  string
	Block Block
}

type Block struct {
	Statements []Statement
}

type Expression struct {
	Type        ExpressionType
	Value       string
	Operator    TokenType
	Expressions []*Expression
}

func (e *Expression) Name() *Expression {
	return e.Expressions[0]
}

func (e *Expression) Args() []*Expression {
	return e.Expressions[1:]
}

func (e *Expression) Right() *Expression {
	return e.Expressions[0]
}

func (e *Expression) Left() *Expression {
	return e.Expressions[1]
}

func (e *Expression) IsLiteral() bool {
	return e.Type == ExpBool ||
		e.Type == ExpNumber ||
		e.Type == ExpString ||
		e.Type == ExpWord
}

type Statement struct {
	Type         StatementType
	Expression   *Expression
	Block        *Block
	Alternatives []*Statement
}

func ParseTokens(tokens []Token) ([]Script, error) {
	var xs []Script
	var i int
	for i < len(tokens) {
		//fmt.Println("next script!", tokens[i], i)
		t := tokens[i]
		if t.Type != TokWord {
			return xs, errors.New(fmt.Sprintf("Expected a Word token at top level, got %s.", t.String()))
		}
		if keyword := LiteralToKeyword(t.Literal); keyword != KeyScript {
			return xs, errors.New(fmt.Sprintf("Expected token keyword 'script', got '%s'", t.Literal))
		}
		script, err := parseScript(tokens, &i)
		if err != nil {
			return nil, err
		}
		xs = append(xs, script)
		//fmt.Println("end script", tokens[i], i, len(tokens))
	}
	return xs, nil
}

func parseScript(tokens []Token, i *int) (Script, error) {
	var s Script
	if *i+2 > len(tokens) {
		return s, errors.New("Invalid syntax for script declaration, expected 2 more tokens.")
	}
	// expect next token to be a word, expect token after to be :, and then an indent
	word := tokens[*i+1]
	col := tokens[*i+2]
	if word.Type != TokWord {
		return s, errors.New("Expected a word token after script.")
	}
	s.Name = word.Literal
	if col.Type != TokColon {
		return s, errors.New("Expected a colon token after script word.")
	}
	*i = *i + 2 // Go past name to :
	block, err := parseBlock(tokens, i, 0)
	if err != nil {
		return s, err
	}
	s.Block = block
	return s, nil
}

func parseBlock(tokens []Token, i *int, indent uint) (Block, error) {
	var b Block
	var count uint
	*i++ // advance past :
	for *i < len(tokens) {
		//fmt.Println("parseBlock", tokens[*i], indent)
		t := tokens[*i]
		if t.Type != TokIndent {
			// must be end of block, exit here
			//fmt.Println("breaking block on", tokens[*i])
			break
		}
		count++
		if count < indent+1 {
			*i = *i + 1
			continue
		}
		stmt, err := parseStatement(tokens, i, indent+1)
		if err != nil {
			return b, err
		}
		b.Statements = append(b.Statements, stmt)
		// Reset our indent reading counter after each statement so we can
		// start at the same start line.
		count = 0
	}
	//fmt.Println("indent lvl finished with", count)
	// Remove any extra indents read so statements line up for previous blocks.
	*i -= int(count)
	return b, nil
}

func parseStatement(tokens []Token, i *int, indent uint) (Statement, error) {
	var s Statement
	var err error
	*i++ // advance past the current indent
	for *i < len(tokens) {
		t := tokens[*i]
		if t.Type == TokIndent {
			// XXX throw invalid indentation error here???
			break
		}
		kw := LiteralToKeyword(t.Literal)
		//fmt.Println("Parsing stmt", t, kw)
		switch kw {
		case KeyIf:
			s, err = parseConditional(tokens, i, indent)
			if err != nil {
				break
			}
		case KeyRun:
			s.Type = StmtRun
			*i++ // go past run to exp
			s.Expression, err = parseExpression(tokens, i, PrecLowest)
			if err != nil {
				return s, err
			}
			break
		case KeyEnd:
			s.Type = StmtEnd
			*i += 1 // go past end
			break
		default:
			s.Type = StmtExpression
			s.Expression, err = parseExpression(tokens, i, PrecLowest)
			if err != nil {
				return s, err
			}
		}
		break
	}
	if err != nil {
		return s, err
	}
	if s.Type == StmtInvalid {
		return s, errors.New("Statement could not be parsed.")
	}
	return s, nil
}

func parseConditional(tokens []Token, i *int, indent uint) (Statement, error) {
	//fmt.Println("parseConditional", tokens[*i])
	var s Statement
	var err error
	s.Type = StmtIf
	*i++ // go past if token
	s.Expression, err = parseExpression(tokens, i, PrecLowest)
	if err != nil {
		return s, err
	}
	if tokens[*i].Type != TokColon {
		fmt.Println(tokens[*i])
		return s, errors.New("Expected conditional block to begin with a colon.")
	}
	block, err := parseBlock(tokens, i, indent)
	if err != nil {
		return s, err
	}
	s.Block = &block
loop:
	for *i+int(indent) < len(tokens) && tokens[*i+int(indent)].Type == TokWord {
		*i += int(indent) // Go past the indent we were left on from previous if block
		//fmt.Println("if loop", tokens[*i], indent)
		switch LiteralToKeyword(tokens[*i].Literal) {
		case KeyElif:
			*i++ // go past elif token
			exp, err := parseExpression(tokens, i, PrecLowest)
			if err != nil {
				return s, err
			}
			block, err := parseBlock(tokens, i, indent)
			if err != nil {
				return s, err
			}
			s.Alternatives = append(s.Alternatives, &Statement{
				Type:       StmtElif,
				Expression: exp,
				Block:      &block,
			})
		case KeyElse:
			*i++ // go past else token
			block, err := parseBlock(tokens, i, indent)
			if err != nil {
				return s, err
			}
			s.Alternatives = append(s.Alternatives, &Statement{
				Type:  StmtElse,
				Block: &block,
			})
			break loop
		default:
			*i -= int(indent) // revert our indent because we didn't use it
			break loop
		}
	}
	return s, nil
}

func parseExpression(tokens []Token, i *int, prec uint) (*Expression, error) {
	if *i >= len(tokens) {
		return nil, errors.New("Invalid expression: EOT")
	}
	//fmt.Println("parseExpression: ", tokens[*i])
	t := tokens[*i]
	var left *Expression
	var err error
	if isPrefixToken(t) {
		left, err = parsePrefixExp(tokens, i)
	} else {
		left, err = parseLiteralExp(tokens, i)
	}
	if err != nil {
		return nil, err
	}
	if *i+1 >= len(tokens) {
		//fmt.Println("Going past: ", tokens[*i])
		*i++
		return left, nil
	}
	peek := tokens[*i+1]
	*i++ // Go past left
	for isExpToken(peek) && prec < getTokenPrecedence(peek) {
		//fmt.Println("doing peeky", isExpToken(peek), prec, getTokenPrecedence(peek), peek)
		if !isInfixToken(peek) {
			break
		}
		left, err = parseInfixExp(tokens, i, left)
		if err != nil {
			return nil, err
		}
		if *i+1 >= len(tokens) {
			break
		}
		peek = tokens[*i+1]
	}
	//if *i < len(tokens) {
	//	fmt.Println("ending with", tokens[*i])
	//}
	return left, nil
}

func parseLiteralExp(tokens []Token, i *int) (*Expression, error) {
	t := tokens[*i]
	exp := &Expression{}
	if t.Type == TokColon || t.Type == TokIndent {
		return nil, ErrInvalidExpressionToken
	}
	keyword := LiteralToKeyword(t.Literal)
	if keyword == KeyTrue || keyword == KeyFalse {
		exp.Type = ExpBool
	} else if keyword != KeyInvalid {
		return nil, ErrInvalidExpressionToken
	} else {
		switch t.Type {
		case TokWord:
			exp.Type = ExpWord
		case TokString:
			exp.Type = ExpString
		case TokNumber:
			exp.Type = ExpNumber
		default:
			return nil, errors.New(fmt.Sprintf("Could not parse token as literal: %s", t.String()))
		}
	}
	exp.Value = t.Literal
	return exp, nil
}

func parsePrefixExp(tokens []Token, i *int) (*Expression, error) {
	//fmt.Println("Parsing prefix", tokens[*i])
	op := tokens[*i].Type
	*i++ // go past operator
	right, err := parseExpression(tokens, i, PrecPrefix)
	if err != nil {
		return nil, err
	}
	*i-- // go back to ending expression as it goes too far in parseExpression
	return &Expression{
		Type:        ExpPrefix,
		Operator:    op,
		Expressions: []*Expression{right},
	}, nil
}

func parseInfixExp(tokens []Token, i *int, left *Expression) (*Expression, error) {
	//fmt.Println("parseInfixExp:", tokens[*i], left)
	// Parse function calls
	if left.Type == ExpWord && tokens[*i].Type == TokLParen {
		return parseCallExp(tokens, i, left)
	}
	// Parse operators
	prec := getTokenPrecedence(tokens[*i])
	op := tokens[*i].Type
	*i++ // Go past operator
	right, err := parseExpression(tokens, i, prec)
	if err != nil {
		return nil, err
	}
	return &Expression{
		Type:        ExpInfix,
		Operator:    op,
		Expressions: []*Expression{right, left},
	}, nil
}

func parseCallExp(tokens []Token, i *int, lit *Expression) (*Expression, error) {
	//fmt.Println("parseCallExp:", tokens[*i], lit)
	exp := &Expression{
		Type:        ExpCall,
		Expressions: []*Expression{lit},
	}
	*i++ // Go past (
	for tokens[*i].Type != TokRParen {
		t := tokens[*i]
		if t.Type == TokComma {
			*i++ // Go past ,
			continue
		}
		//fmt.Println("parseCallExp argloop", tokens[*i], i)
		arg, err := parseExpression(tokens, i, PrecLowest)
		if err != nil {
			return nil, err
		}
		exp.Expressions = append(exp.Expressions, arg)
	}
	*i++ // Go past )
	return exp, nil
}

func LiteralToKeyword(str string) Keyword {
	switch str {
	case "script":
		return KeyScript
	case "if":
		return KeyIf
	case "elif":
		return KeyElif
	case "else":
		return KeyElse
	case "run":
		return KeyRun
	case "end":
		return KeyEnd
	case "eq":
		return KeyEq
	case "true":
		return KeyTrue
	case "false":
		return KeyFalse
	}
	return KeyInvalid
}
