package script

import (
	"errors"
	"fmt"
)

var (
	ErrExpNotBoolean       = errors.New("Cannot evaluate non-boolean expression.")
	ErrBadUseMinus         = errors.New("Can only use '-' operator on numbers.")
	ErrBadUseBang          = errors.New("Can only use '!' operator on boolean expressions.")
	ErrUnhandledExpression = errors.New("Unhandled expression.")
	ErrUnhandledInfixExp   = errors.New("Unhandled infix expression.")
	ErrUnhandledStatement  = errors.New("Unhandled statement.")
)

type Call struct {
	Name   string
	Args   []*Value
	Finish chan CallResult
}

type CallResult struct {
	Value Value
	Error error
}

type Program struct {
	Scripts map[string]Script
	Strings map[string]string
	Globals map[string]Value
}

type RunContext struct {
	*Program
	Call     chan *Call
	Complete chan error
}

func (p *Program) Run(name string) RunContext {
	ctx := RunContext{
		Program:  p,
		Call:     make(chan *Call, 1),
		Complete: make(chan error, 1),
	}
	go func() {
		ctx.Complete <- ctx.Evaluate(name)
		close(ctx.Complete)
		close(ctx.Call)
	}()
	return ctx
}

func (p *RunContext) Evaluate(name string) error {
	script, ok := p.Scripts[name]
	if !ok {
		return errors.New(fmt.Sprintf("Script '%s' not defined.", name))
	}
	_, err := p.evaluateBlock(&script.Block)
	return err
}

func (p *RunContext) evaluateBlock(block *Block) (bool, error) {
	for _, st := range block.Statements {
		done, err := p.evaluateStatement(&st)
		if err != nil {
			return false, err
		}
		if done {
			return true, nil
		}
	}
	return false, nil
}

func (p *RunContext) evaluateStatement(st *Statement) (bool, error) {
	switch st.Type {
	case StmtExpression:
		_, err := p.evaluateExpression(st.Expression)
		if err != nil {
			return false, err
		}
	case StmtEnd:
		return true, nil
	case StmtRun:
		return true, p.Evaluate(st.Expression.Value)
	case StmtIf:
		return p.evaluateConditional(st)
	default:
		return false, ErrUnhandledStatement
	}
	return false, nil
}

func (p *RunContext) evaluateExpression(exp *Expression) (*Value, error) {
	switch exp.Type {
	case ExpBool:
		return newValueFromBoolStr(exp.Value), nil
	case ExpNumber:
		return newValueFromFloatStr(exp.Value), nil
	case ExpString:
		return NewStringValue(exp.Value), nil
	case ExpWord:
		val, ok := p.Globals[exp.Value]
		if !ok {
			return nil, errors.New(fmt.Sprintf("Keyword '%s' not defined.", exp.Value))
		}
		return &val, nil
	case ExpCall:
		xs := exp.Args()
		call := &Call{
			Name:   exp.Name().Value,
			Args:   make([]*Value, len(xs)),
			Finish: make(chan CallResult, 1),
		}
		for i, x := range xs {
			var err error
			call.Args[i], err = p.evaluateExpression(x)
			if err != nil {
				return nil, err
			}
		}
		p.Call <- call
		res := <-call.Finish
		close(call.Finish)
		if res.Error != nil {
			return nil, res.Error
		}
		return &res.Value, nil
	case ExpPrefix:
		return p.evaluatePrefixExp(exp)
	case ExpInfix:
		return p.evaluateInfixExp(exp)
	}
	return nil, ErrUnhandledExpression
}

func (p *RunContext) evaluatePrefixExp(exp *Expression) (*Value, error) {
	if exp.Operator == TokDollar {
		key := exp.Right().Value
		str, ok := p.Strings[key]
		if !ok {
			return nil, errors.New(fmt.Sprintf("Key string not found for '%s'.", key))
		}
		return NewStringValue(str), nil
	}
	res, err := p.evaluateExpression(exp.Right())
	if err != nil {
		return nil, err
	}
	switch exp.Operator {
	case TokMinus:
		if res.Type != ValNumber {
			return nil, ErrBadUseMinus
		}
		return NewNumberValue(res.Number() * -1), nil
	case TokNot:
		if !res.Boolean() {
			return nil, ErrBadUseBang
		}
		return NewBoolValue(res.Bool()), nil
	}
	return nil, errors.New(fmt.Sprintf("Unhandled prefix operator %d.", exp.Operator))
}

func (p *RunContext) evaluateInfixExp(exp *Expression) (*Value, error) {
	left, err := p.evaluateExpression(exp.Left())
	if err != nil {
		return nil, err
	}
	if !left.Boolean() {
		return nil, ErrExpNotBoolean
	}
	right, err := p.evaluateExpression(exp.Right())
	if err != nil {
		return nil, err
	}
	if !right.Boolean() {
		return nil, ErrExpNotBoolean
	}
	switch exp.Operator {
	case TokAnd:
		return NewBoolValue(left.Bool() && right.Bool()), nil
	case TokOr:
		return NewBoolValue(left.Bool() || right.Bool()), nil
	default:
		return nil, ErrUnhandledInfixExp
	}
}

func (p *RunContext) evaluateConditional(st *Statement) (bool, error) {
	res, err := p.evaluateExpression(st.Expression)
	if err != nil {
		return false, err
	}
	if !res.Boolean() {
		return false, ErrExpNotBoolean
	}
	if res.Bool() {
		return p.evaluateBlock(st.Block)
	}
	// XXX would be nice to merge if exp with this for loop
	for _, alt := range st.Alternatives {
		if alt.Type == StmtElse {
			return p.evaluateBlock(alt.Block)
		}
		res, err := p.evaluateExpression(alt.Expression)
		if err != nil {
			return false, err
		}
		if !res.Boolean() {
			return false, ErrExpNotBoolean
		}
		if res.Bool() {
			return p.evaluateBlock(alt.Block)
		}
	}
	return false, nil
}
