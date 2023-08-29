package script

import (
	"encoding/binary"
	"fmt"
	"math"
	"strconv"
)

type ValueType uint8

const (
	ValNil ValueType = iota
	ValBool
	ValNumber
	ValString
)

type Value struct {
	Type ValueType
	Buf  []byte
}

var NilValue = Value{}

func (v *Value) ToString() string {
	switch v.Type {
	case ValNil:
		return "(nil)"
	case ValBool:
		if v.Bool() {
			return "TRUE"
		}
		return "FALSE"
	case ValNumber:
		f64 := v.Number()
		return fmt.Sprintf("%.05f", f64)
	case ValString:
		return v.String()
	default:
		return "Invalid Type"
	}
}

func (v *Value) String() string {
	return string(v.Buf)
}

func (v *Value) Number() float64 {
	u := binary.BigEndian.Uint64(v.Buf)
	return math.Float64frombits(u)
}

func (v *Value) NumberAsInt() int {
	n := v.Number()
	return int(n)
}

func (v *Value) Boolean() bool {
	return v.Type == ValNil || v.Type == ValBool || v.Type == ValNumber
}

func (v *Value) Bool() bool {
	switch v.Type {
	case ValNil:
		return false
	case ValNumber:
		return v.Number() != 0
	}
	return v.Buf[0] == 1
}

func (v *Value) IsNil() bool {
	return v.Type == ValNil
}

func NewStringValue(str string) *Value {
	return &Value{
		Type: ValString,
		Buf:  []byte(str),
	}
}

func NewNumberValue(f float64) *Value {
	var buf [8]byte
	binary.BigEndian.PutUint64(buf[:], math.Float64bits(f))
	return &Value{
		Type: ValNumber,
		Buf:  buf[:],
	}
}

func NewBoolValue(b bool) *Value {
	var buf [1]byte
	if b {
		buf[0] = 1
	}
	return &Value{
		Type: ValBool,
		Buf:  buf[:],
	}
}

func newValueFromFloatStr(str string) *Value {
	f64, _ := strconv.ParseFloat(str, 64)
	return NewNumberValue(f64)
}

func newValueFromBoolStr(str string) *Value {
	var b bool
	if str == "true" {
		b = true
	}
	return NewBoolValue(b)
}
