package script

import (
	"fmt"
)

func PrintScript(s Script) {
	fmt.Printf("script %s:\n", s.Name)
	PrintBlock(s.Block, 1)
}

func PrintBlock(b Block, indent uint) {
	for _, stmt := range b.Statements {
		for i := uint(0); i < indent; i++ {
			fmt.Printf("\t")
		}
		PrintStatement(stmt, indent+1)
	}
}

func PrintStatement(s Statement, indent uint) {
	switch s.Type {
	case StmtEnd:
		fmt.Printf("end\n")
	case StmtRun:
		fmt.Printf("run ")
		PrintExpression(*s.Expression)
	case StmtIf:
		fmt.Printf("if %s:\n", SprintExpression(*s.Expression))
		PrintBlock(*s.Block, indent)
		for _, alt := range s.Alternatives {
			for i := uint(0); i < indent-1; i++ {
				fmt.Printf("\t")
			}
			PrintStatement(*alt, indent)
		}
	case StmtElif:
		fmt.Printf("elif %s:\n", SprintExpression(*s.Expression))
		PrintBlock(*s.Block, indent)
	case StmtElse:
		fmt.Printf("else:\n")
		PrintBlock(*s.Block, indent)
	case StmtExpression:
		PrintExpression(*s.Expression)
	case StmtInvalid:
		fmt.Printf("Invalid Statement\n")
	}
}

func SprintExpression(e Expression) string {
	if e.IsLiteral() {
		return fmt.Sprintf("(%v)", e.Value)
	} else {
		switch e.Type {
		case ExpCall:
			str := fmt.Sprintf("(%s(", e.Name().Value)
			for _, arg := range e.Args() {
				str += SprintExpression(*arg)
				str += ", "
			}
			str += "))"
			return str
		case ExpInfix:
			return fmt.Sprintf("(%s %s %s)",
				SprintExpression(*e.Left()),
				e.Operator.String(),
				SprintExpression(*e.Right()),
			)
		case ExpPrefix:
			return fmt.Sprintf("(%s %s)", e.Operator.String(), SprintExpression(*e.Right()))
		}
	}
	return fmt.Sprintf("Unknown Expression(%d:%s)", e.Type, e.Value)
}

func PrintExpression(e Expression) {
	fmt.Println(SprintExpression(e))
}
