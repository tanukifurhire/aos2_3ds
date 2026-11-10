package main
import ("debug/pe";"fmt";"os";"strconv")
func main(){f,_:=pe.Open(os.Args[1]);base:=f.OptionalHeader.(*pe.OptionalHeader32).ImageBase
for _,a:=range os.Args[2:]{o,_:=strconv.ParseUint(a,0,32)
for _,s:=range f.Sections{if uint32(o)>=s.Offset&&uint32(o)<s.Offset+s.Size{fmt.Printf("%s off=%#x va=%#x\n",s.Name,o,base+s.VirtualAddress+uint32(o)-s.Offset)}}}}
