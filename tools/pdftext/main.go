package main
import ("fmt";"os";"github.com/ledongthuc/pdf")
func main(){f,r,err:=pdf.Open(os.Args[1]);if err!=nil{panic(err)};defer f.Close()
for i:=1;i<=r.NumPage();i++{p:=r.Page(i);if p.V.IsNull(){continue}
rows,_:=p.GetTextByRow();fmt.Printf("=== page %d\n",i)
for _,row:=range rows{s:="";for _,w:=range row.Content{s+=w.S};fmt.Println(s)}}}
