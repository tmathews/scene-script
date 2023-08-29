package script

import (
	"testing"
)

func runScriptStr(str string) (Script, error) {
	var s Script
	tokens, err := Lexer([]rune(str))
	if err != nil {
		return s, err
	}
	xs, err := ParseTokens(tokens)
	if err != nil {
		return s, err
	}
	return xs[0], nil
}

func TestParseTokens(t *testing.T) {
	var str string
	var err error
	//var scripts []Script
	// Empty script should work
	str = "script Entry:"
	if _, err := runScriptStr(str); err != nil {
		t.Error(err)
	}
	// Basic expression should work
	str = "script Entry:\n\t`i am string`"
	if _, err := runScriptStr(str); err != nil {
		t.Error(err)
	}
	// Simple script calling another script should work
	str = "script Entry:\n\trun OtherScript"
	if _, err := runScriptStr(str); err != nil {
		t.Error(err)
	}
	// Simple script with end call should work
	str = "script Entry:\n\tend"
	if _, err := runScriptStr(str); err != nil {
		t.Error(err)
	}
	// Basic call should work
	str = "script Entry:\n\tHelloWorld()"
	if _, err = runScriptStr(str); err != nil {
		t.Error(err)
	}
	// Basic call with argument should work
	str = "script Entry:\n\tHelloWorld(1)"
	if _, err = runScriptStr(str); err != nil {
		t.Error(err)
	}
	// Basic call with arguments should work
	str = "script Entry:\n\tHelloWorld(1, `hello`, true)"
	if _, err = runScriptStr(str); err != nil {
		t.Error(err)
	}
	// Too short indentation should fail
	str = "script Entry:\n\tHelloWorld()\nend"
	if _, err = runScriptStr(str); err == nil {
		t.Errorf("Should have errored with invalid indentation, reason: too short.")
	}
	// Test invalid indentation: too long
	str = "script Entry:\n\t\tHelloWorld()\t\tend"
	if _, err = runScriptStr(str); err == nil {
		t.Errorf("Should have errored with invalid indentation, reason: too long.")
	}
	// Test multline strings
	str = "script Entry:\n\t`multiline\nstring\nshould\nwork.`"
	if _, err = runScriptStr(str); err != nil {
		t.Error(err)
	}
}

func TestParseNumber(t *testing.T) {
	str := `
script Entry:
	if 1.1:
		end
`
	script, err := runScriptStr(str)
	if err != nil {
		t.Error(err)
	}
	PrintScript(script)
}

func TestParseConditional(t *testing.T) {
	str := `
script Entry:
	if 1:
		end
`
	if _, err := runScriptStr(str); err != nil {
		t.Error(err)
	}
}

func TestParseConditionalEmpty(t *testing.T) {
	str := `
script Entry:
	if 1:
`
	if _, err := runScriptStr(str); err != nil {
		t.Error(err)
	}
}

func TestParseConditionalNested(t *testing.T) {
	str := `
script Entry:
	if 1:
		if 2:
			end
		elif 3:
	else:
		3
`
	s, err := runScriptStr(str)
	if err != nil {
		t.Error(err)
	}
	if len(s.Block.Statements) != 1 {
		t.Errorf("Should have gotten 1 statements for root script block, got %d.", len(s.Block.Statements))
	} else if s.Block.Statements[0].Type != StmtIf {
		t.Errorf("Expected statement 2 to be of StmtElse, got %d.", s.Block.Statements[1].Type)
	} else {
		alts := s.Block.Statements[0].Alternatives
		if len(alts) != 1 {
			t.Errorf("Expected 1 alternative statement, got %d.", len(alts))
		} else if alts[0].Type != StmtElse {
			t.Errorf("Expected else statement, got %d.", alts[0].Type)
		}
	}
	//PrintScript(s)
}

func TestParseConditionalMulti(t *testing.T) {
	str := `
script Entry:
	if 1:
		run HelloThere
	if 2:
		end
`
	_, err := runScriptStr(str)
	if err != nil {
		t.Error(err)
	}
}

func TestParseConditionalElse(t *testing.T) {
	str := `
script Entry:
	if 1:
		HelloJane()
	else:
		HelloBob()
`
	_, err := runScriptStr(str)
	if err != nil {
		t.Error(err)
	}
}

func TestParseConditionalElif(t *testing.T) {
	str := `
script Entry:
	if 1:
		HelloJane()
	elif 2:
		HelloBob()
`
	_, err := runScriptStr(str)
	if err != nil {
		t.Error(err)
	}
}

func TestParseConditionalIfChain(t *testing.T) {
	str := `
script Entry:
	if 1:
		HelloJane()
	elif 2:
		HelloBob()
	else:
		HelloWorld()
		end
`
	_, err := runScriptStr(str)
	if err != nil {
		t.Error(err)
	}
}
