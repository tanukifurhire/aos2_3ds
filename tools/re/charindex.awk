# input: /tmp/reg.txt lines "<decaddr> <funchex>"
BEGIN{
 split("Sora Alte Tsih Mira Sham Nath StarBreaker Suguri Saki Iru Nanako Kae Kyoko Hime Sumika SumikaB",N," ")
}
{ a=$1; f=strtonum("0x"$2);
  if (a>=0x881430 && a<0x881430+16*16){i=int((a-0x881430)/16); if(f>EN[i])EN[i]=f}
  if (a>=0x8813bc && a<0x8813bc+64){LOAD[int((a-0x8813bc)/4)]=f}
  if (a>=0x88131c && a<0x88131c+64){TA[int((a-0x88131c)/4)]=f}
  if (a>=0x88136c && a<0x88136c+64){TB[int((a-0x88136c)/4)]=f}
  if (a>=0x8809a0 && a<0x8809a0+604*4){ST[(a-0x8809a0)/4]=f}
  if (a>=0x880030 && a<0x880030+604*4){OB[(a-0x880030)/4]=f}
}
END{
 n=0; for(i=0;i<16;i++){ord[n++]=i}
 # sort chars by block end
 for(i=0;i<16;i++)for(j=i+1;j<16;j++)if(EN[ord[j]]<EN[ord[i]]){t=ord[i];ord[i]=ord[j];ord[j]=t}
 prev=0x69d000; # first character code starts around Alte's loader region
 for(k=0;k<16;k++){c=ord[k]; if(k>0 && EN[c]==EN[ord[k-1]])continue; S[c]=prev; E[c]=EN[c]+0x7ff; prev=E[c]+1}
 for(k=0;k<16;k++){c=ord[k]; if(!(c in S))continue
   printf "== %d %s block %06x..%06x loader=%06x input=%06x tb=%06x\n",c,N[c+1],S[c],E[c],LOAD[c],TA[c],TB[c]
   s="  states:"; for(id=0;id<604;id++) if((id in ST) && ST[id]>=S[c] && ST[id]<=E[c]) s=s sprintf(" %x=%06x",id,ST[id]); print s
   s="  objects:"; for(id=0;id<604;id++) if((id in OB) && OB[id]>=S[c] && OB[id]<=E[c]) s=s sprintf(" %x=%06x",id,OB[id]); print s }
 s="== common states:"; for(id=0;id<604;id++) if((id in ST) && ST[id]<0x69d000) s=s sprintf(" %x=%06x",id,ST[id]); print s
 s="== common objects:"; for(id=0;id<604;id++) if((id in OB) && OB[id]<0x69d000) s=s sprintf(" %x=%06x",id,OB[id]); print s
}
