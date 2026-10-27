/* Live selection and control-group data, seated in the approved RA card. */
static unsigned char g_rahCard[272*166*4];
static void rah_card_pixels(const H6_Card& c){
 memset(g_rahCard,0,sizeof g_rahCard);RAH_Surface s={g_rahCard,272,166};rah_art(s,"card",0,0);
 char line[64];rah_text(s,c.name[0]?c.name:c.code,136,18,"font",true,208);
 rah_raw(s,c.cameo,64,48,34,40,64,48);rah_text(s,"HEALTH",108,41,"gold");rah_well(s,108,55,128,8);
 const int colors[3][3]={{108,176,86},{231,180,54},{215,58,45}};int hc=std::max(0,std::min(2,c.health_color));int fill=c.maxstr>0?std::max(0,std::min(124,c.str*124/c.maxstr)):0;rah_rect(s,110,57,fill,4,colors[hc][0],colors[hc][1],colors[hc][2]);
 snprintf(line,sizeof line,"%d / %d",c.str,c.maxstr);rah_text(s,line,108,72);
 for(int k=0;k<c.nmini;k++){int x=108+k*20;rah_raw(s,c.mini[k].rgba,64,48,x,82,16,12);if(c.overflow&&k==c.nmini-1){snprintf(line,sizeof line,"+%d",c.overflow);rah_text(s,line,x,84,"gold",false,18);}if(c.mini[k].frame)rah_outline(s,x,82,16,12);}
 snprintf(line,sizeof line,"%d UNIT%s SELECTED",c.count,c.count==1?"":"S");rah_text(s,line,34,96,"font",false,204);rah_text(s,"CONTROL GROUPS",34,113,"gold");
 for(int k=0;k<10;k++){int x=33+21*k,f=c.group_frame[k]==3?3:c.group_frame[k]==2?1:0;rah_art(s,"group",x,130,f);snprintf(line,sizeof line,"%d",(k+1)%10);rah_text(s,line,x+9,135,c.group_count[k]?"font":"muted",true);}
}
