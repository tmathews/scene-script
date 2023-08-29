package script

import (
	"os"
	"testing"
)

func TestLexer(t *testing.T) {
	var err error
	buf, err := os.ReadFile("./demo.script")
	if err != nil {
		t.Fatal(err)
	}
	_, err = Lexer([]rune(string(buf)))
	if err != nil {
		t.Fatal(err)
	}
	//for _, t := range tokens {
	//	fmt.Println(t.String())
	//}
}
