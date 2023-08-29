package script

import (
	"testing"
	"time"
)

func newTestProgram(t *testing.T, str string) (*Program, string) {
	tokens, err := Lexer([]rune(str))
	if err != nil {
		t.Fatal(err)
	}
	xs, err := ParseTokens(tokens)
	if err != nil {
		t.Fatal(err)
	}
	p := &Program{
		Scripts: make(map[string]Script),
		Strings: make(map[string]string),
	}
	for _, script := range xs {
		p.Scripts[script.Name] = script
	}
	return p, xs[0].Name
}

func TestProgram_EvaluateSimple(t *testing.T) {
	str := "script Entry:\n\ttrue"
	p, first := newTestProgram(t, str)
	ctx := p.Run(first)
loop:
	for {
		select {
		case err := <-ctx.Complete:
			if err != nil {
				t.Error(err)
			}
			break loop
		case call := <-ctx.Call:
			call.Finish <- CallResult{}
		}
	}
}

func TestProgram_EvaluateCall(t *testing.T) {
	str := "script Entry:\n\tHelloWorld()"
	p, first := newTestProgram(t, str)
	var count int
	ctx := p.Run(first)
loop:
	for {
		select {
		case err := <-ctx.Complete:
			if err != nil {
				t.Fatal(err)
			}
			break loop
		case call := <-ctx.Call:
			switch call.Name {
			case "HelloWorld":
				count++
			}
			call.Finish <- CallResult{}
		}
	}
	if count != 1 {
		t.Errorf("Expected HelloWorld to be called once, got %d.", count)
	}
}

func TestProgramPause(t *testing.T) {
	str := "script Entry:\n\tPauseResume()"
	p, first := newTestProgram(t, str)
	var activeCall *Call
	ctx := p.Run(first)
loop:
	for {
		select {
		case err := <-ctx.Complete:
			if err != nil {
				t.Fatal(err)
			}
			if err == nil {
				t.Fatalf("Should not have resolved yet.")
			}
		case call := <-ctx.Call:
			switch call.Name {
			case "PauseResume":
				activeCall = call
				break loop
			}
		}
	}
	t.Log("Sleeping 200ms")
	time.Sleep(time.Millisecond * 200)
	activeCall.Finish <- CallResult{}
loop2:
	for {
		select {
		case err := <-ctx.Complete:
			if err != nil {
				t.Fatal(err)
			}
			break loop2
		}
	}
}

func TestProgram_EvaluateConditional(t *testing.T) {
	str := `
script Entry:
	HelloWorld()
	if 1:
		HelloWorld()
	else:
		true
	if false:
		end
	else:
		HelloWorld()
	if HelloWorld():
		HelloWorld()
`
	p, first := newTestProgram(t, str)
	var count int
	expected := 4
	ctx := p.Run(first)
loop:
	for {
		select {
		case err := <-ctx.Complete:
			if err != nil {
				t.Fatal(err)
			}
			break loop
		case call := <-ctx.Call:
			switch call.Name {
			case "HelloWorld":
				count++
			}
			call.Finish <- CallResult{}
		}
	}
	if count != expected {
		t.Errorf("Expected HelloWorld to be called %d times, got %d.", expected, count)
	}
}

func TestProgram_EvaluateConditionalAndOr(t *testing.T) {
	str := `
script Entry:
	if false:
		HelloWorld()
	if 1 and 2:
		HelloWorld()
	if false or 0:
		HelloWorld()
	else:
		HelloWorld()`
	p, first := newTestProgram(t, str)
	var count int
	expected := 2
	ctx := p.Run(first)
loop:
	for {
		select {
		case err := <-ctx.Complete:
			if err != nil {
				t.Fatal(err)
			}
			break loop
		case call := <-ctx.Call:
			switch call.Name {
			case "HelloWorld":
				count++
			}
			call.Finish <- CallResult{}
		}
	}
	if count != expected {
		t.Errorf("Expected HelloWorld to be called %d times, got %d.", expected, count)
	}
}

func TestProgram_EvaluateKeyString(t *testing.T) {
	str := `
script Entry:
	HelloWorld($KeyString, 1)`
	p, first := newTestProgram(t, str)
	p.Strings["KeyString"] = "It's a key string"
	var call *Call
	ctx := p.Run(first)
loop:
	for {
		select {
		case err := <-ctx.Complete:
			if err != nil {
				t.Fatal(err)
			}
			break loop
		case call = <-ctx.Call:
			call.Finish <- CallResult{}
		}
	}
	if call == nil {
		t.Fatalf("Expected call to be set.")
	}
	if len(call.Args) != 2 {
		t.Fatalf("Expected 2 argument, got %d.", len(call.Args))
	}
	arg := call.Args[0]
	if arg.Type != ValString {
		t.Errorf("Arg type was not ValString, got %d.", arg.Type)
	}
	if arg.String() != p.Strings["KeyString"] {
		t.Errorf("Arg value did not match '%s', got '%s'.", p.Strings["KeyString"], arg.String())
	}
}

func TestProgram_EvaluateCallArgTypes(t *testing.T) {
	str := "script Entry:\n\tHelloWorld(`String`, 1337, 256.01, false)"
	p, first := newTestProgram(t, str)
	var call *Call
	ctx := p.Run(first)
loop:
	for {
		select {
		case err := <-ctx.Complete:
			if err != nil {
				t.Fatal(err)
			}
			break loop
		case call = <-ctx.Call:
			call.Finish <- CallResult{}
		}
	}
	if call == nil {
		t.Fatalf("Expected call to be set.")
	}
	if len(call.Args) != 4 {
		t.Fatalf("Expected 4 arguments, got %d.", len(call.Args))
	}
	arg := call.Args[0]
	if arg.Type != ValString {
		t.Errorf("Arg type was not ValString, got %d.", arg.Type)
	}
	arg = call.Args[1]
	if arg.Type != ValNumber {
		t.Errorf("Arg type was not ValNumber, got %d", arg.Type)
	} else {
		t.Logf("Number Value: %f", arg.Number())
	}
	arg = call.Args[2]
	if arg.Type != ValNumber {
		t.Errorf("Arg type was not ValNumber, got %d", arg.Type)
	} else {
		t.Logf("Number Value: %f", arg.Number())
	}
	arg = call.Args[3]
	if arg.Type != ValBool {
		t.Errorf("Arg type was not ValBool, got %d", arg.Type)
	} else {
		t.Logf("Bool Value: %t", arg.Bool())
	}
}
