/* Approved RA v06 presentation. All gameplay state/orders remain in cnc_sidebar.h. */
static std::map<std::string,std::vector<unsigned char> > g_rahCameos;
static unsigned char g_rahBar[RAH_W*RAH_H*4],g_rahWing[RAH_WING_W*RAH_WING_H*4],g_rahHeader[RAH_W*17*4];
static GLuint g_rahTex=0,g_rahWingTex=0,g_rahHeadTex[4]={0};
static const unsigned char *rah_cameo_plain(const char *name){
 const char *art=sb_art_name(name);std::vector<unsigned char>& out=g_rahCameos[art];if(!out.empty())return out.data();
 out.assign(64*48*4,0);const DB_Pack *p=g_ra95Pack;const DB_Shape *a=p?db_shape(p,art):0;
 if(!a){p=g_dbPack;a=p?db_shape(p,art):0;}if(!a)return out.data();
 std::vector<unsigned char> ix(a->w*a->h);DB_Surface surf;db_surface_init(&surf,a->w,a->h,ix.data());db_draw_shape(&surf,a,0,0,0);
 for(int y=0;y<48;y++)for(int x=0;x<64;x++){unsigned c=ix[(y*a->h/48)*a->w+x*a->w/64];unsigned char *q=out.data()+(y*64+x)*4;memcpy(q,p->pal8+c*3,3);q[3]=255;}
 return out.data();
}
static int rah_skin(SbHit hit,int index=-1,bool active=false,bool disabled=false){
 if(disabled)return 4;if(g_sbPress==hit&&(index<0||g_sbPressCol==index))return 2;
 if(g_sbHover==hit&&(index<0||g_sbHoverCol==index))return active?5:1;return active?3:0;
}
static void rah_cameo(RAH_Surface dst,const EH_Display& d,int x,int y){
 const SbEntry *e=d.engine>=0?&g_sbState.entry[d.engine]:0;unsigned char image[RAH_CW*RAH_CH*4];RAH_Surface c={image,RAH_CW,RAH_CH};memset(image,0,sizeof image);
 rah_raw(c,rah_cameo_plain(d.name),64,48,0,0,RAH_CW,RAH_CH);
 if(!e){for(int k=0;k<RAH_CW*RAH_CH;k++){unsigned char *q=image+k*4;q[0]=q[1]=q[2]=(q[0]*3+q[1]*6+q[2])/10;q[3]=128;}}
 else if(!e->completed&&(e->constructing||e->onHold||e->busy)){
  const float tm=g_sb.EffectTime?g_sb.EffectTime():0;const float progress=e->constructing||e->onHold?eh_smooth_progress(*e,tm):0;
  const double tau=6.283185307179586,angle=std::max(0.f,std::min(1.f,progress))*tau;
  for(int yy=0;yy<RAH_CH;yy++)for(int xx=0;xx<RAH_CW;xx++){
   double dx=xx+.5-RAH_CW*.5,dy=yy+.5-RAH_CH*.5,a=atan2(dy,dx)+tau*.25;if(a<0)a+=tau;if(a>=tau)a-=tau;
   unsigned char *q=image+(yy*RAH_CW+xx)*4;
   if(a>=angle){double v=(xx+yy)/(double)(RAH_CW+RAH_CH);const double tint[2][3]={{10+7*v,28+11*v,65+13*v},{65+21*v,7+7*v,5+2*v}};double alpha=.90-.08*sin(v*3.14159);for(int k=0;k<3;k++)q[k]=(unsigned char)(q[k]*(1-alpha)+tint[g_sb.nod?1:0][k]*alpha);}
   double vx=sin(angle),vy=-cos(angle),along=dx*vx+dy*vy,dist=fabs(dx*vy-dy*vx);
   if(progress>0&&along>=0&&dist<2.5){double a=(dist<1.5?.75:.22)*(0.85+.15*sin(tm*4));const int col[2][3]={{141,173,212},{236,102,82}};for(int k=0;k<3;k++)q[k]=(unsigned char)(q[k]*(1-a)+col[g_sb.nod?1:0][k]*a);}
  }
 }
 rah_raw(dst,image,RAH_CW,RAH_CH,x,y,RAH_CW,RAH_CH);
 if(e&&(e->completed||e->onHold))rah_text(dst,e->completed?"READY":"HOLD",x+40,y+24,"gold",true);
 if(g_sbHoverCol>=0&&g_sbHoverSlot>=0){const EH_Display *h=eh_display(g_sbHoverCol,g_sbHoverSlot);if(h&&!strcmp(h->name,d.name)&&(g_sbHover==SBH_ITEM||g_sbHover==SBH_LOCKED))rah_outline(dst,x,y,RAH_CW,RAH_CH);}
}
static void rah_draw_panel(const H6_State& st,int fbw,int fbh){
 memset(g_rahBar,0,sizeof g_rahBar);const int height=rah_height(g_h6Rows),base=height-49,foot=height-33;RAH_Surface s={g_rahBar,RAH_W,height};
 rah_art(s,"map",0,18);rah_rect(s,30,34,156,151,0,0,0);if(st.radar_active&&st.radar_rgba)rah_raw(s,st.radar_rgba,156,151,30,34,156,151);else rah_art(s,g_sb.nod?"soviet":"allies",30,34,0,156,151);
 char bodyName[16]="body",glassName[16]="glass";if(g_h6Rows<6){snprintf(bodyName,sizeof bodyName,"body%d",g_h6Rows);snprintf(glassName,sizeof glassName,"glass%d",g_h6Rows);}const H6_Asset *glass=hud640_asset(g_raHudPack,glassName);const int gy=height-glass->h,limit=base-gy-22;rah_art(s,bodyName,0,205);rah_art(s,glassName,0,gy);
 int produced=std::max(0,g_sbState.powerProduced),drained=std::max(0,g_sbState.powerDrained),status=drained>produced*2?2:drained>produced?1:0;
 const int cols[3][2][3]={{{89,255,85},{0,170,0}},{{239,174,85},{215,121,16}},{{255,0,0},{178,0,0}}};int ph=rah_power_height(produced,limit),dh=rah_power_height(drained,limit);
 for(int k=0;k<2;k++)if(ph)rah_rect(s,10+k*2,base-ph,2,ph+1,cols[status][k][0],cols[status][k][1],cols[status][k][2]);
 rah_art(s,"demand",2,base-dh-4);rah_art(s,"corner",0,205);
 static const char *cats[]={"STRUCTURES","INFANTRY","VEHICLES","AIRCRAFT","SHIPS"},*icons[]={"structures","infantry","vehicles","aircraft","ships"};
 for(int k=0;k<5;k++){rah_art(s,"tab",32+31*k,229,rah_skin(SBH_CATEGORY,k,g_ehCategory==k));rah_art(s,icons[k],36+31*k,234);}
 rah_text(s,cats[g_ehCategory],112,263,"gold",true);eh_refresh();
 for(int k=0;k<g_h6Rows*2;k++){int at=g_ehTop[g_ehCategory]*2+k,x=RAH_GRID_X+(k%2)*RAH_DX,y=RAH_GRID_Y+(k/2)*RAH_DY;if(at<(int)g_ehItems.size()){EH_Display d=g_ehItems[at];rah_cameo(s,d,x,y);}else rah_rect(s,x,y,80,60,0,0,0,56);}
 char readout[64];snprintf(readout,sizeof readout,"%d/%d",produced,drained);rah_well(s,30,foot,114,16);rah_text(s,readout,87,foot+4,"gold",true,110);
 const int maxTop=std::max(0,((int)g_ehItems.size()+1)/2-g_h6Rows);
 if(maxTop>0)for(int k=0;k<2;k++){SbHit hit=k?SBH_DOWN:SBH_UP;bool disabled=k?g_ehTop[g_ehCategory]>=maxTop:g_ehTop[g_ehCategory]<=0;int x=k?176:152;rah_art(s,"scroll",x,foot,rah_skin(hit,0,false,disabled));rah_text(s,k?"V":"^",x+9,foot+4,disabled?"muted":"font",true);}
 memset(g_rahWing,0,sizeof g_rahWing);RAH_Surface wing={g_rahWing,85,165};rah_art(wing,"wing",0,0);
 static const char *tools[]={"repair","sell","power"};bool modes[]={g_sbRepairOn,g_sbSellOn,g_sbPowerOn};SbHit hits[]={SBH_REPAIR,SBH_SELL,SBH_POWER};
 for(int k=0;k<3;k++){rah_art(wing,"tool",21,23+42*k,rah_skin(hits[k],-1,modes[k],g_sbSpectator||(k==0&&!g_sbState.repairEnabled)||(k==1&&!g_sbState.sellEnabled)));rah_art(wing,tools[k],29,31+42*k);}
 glPushAttrib(GL_ENABLE_BIT|GL_COLOR_BUFFER_BIT|GL_TEXTURE_BIT|GL_CURRENT_BIT);glDisable(GL_ALPHA_TEST);glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glEnable(GL_TEXTURE_2D);glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_MODULATE);glColor4f(1,1,1,1);
 const float sc=g_h6Scale,x=g_dbX0,y=g_dbY0;
 h6_upload(&g_rahWingTex,g_rahWing,85,165);h6_tex_quad(g_rahWingTex,85,165,x-63*sc,y+27*sc,x+22*sc,y+192*sc);
 h6_upload(&g_rahTex,g_rahBar,216,height);h6_tex_quad(g_rahTex,216,height,x,y,x+216*sc,y+height*sc);
 const char *labels[]={"OPTIONS","DATABASE",readout,"SIDEBAR"};char credits[32];snprintf(credits,sizeof credits,"%d",st.credits);labels[2]=credits;
 const int left=fbw<696*sc?132:160;const int widths[]={left,left,160,216};const float xs[]={0,left*sc,fbw-376*sc,fbw-216*sc};SbHit hh[]={SBH_OPTIONS,SBH_DATABASE,SBH_CREDITS,SBH_SIDEBAR};
 for(int k=0;k<4;k++){if(k==1&&!codex_available())continue;int w=widths[k];memset(g_rahHeader,0,sizeof g_rahHeader);RAH_Surface hs={g_rahHeader,w,17};rah_art(hs,k==3?"sidebar":w==132?"header132":"header",0,0,rah_skin(hh[k],-1,k==3&&g_h6SlideTarget));rah_text(hs,labels[k],w/2,4,"font",true,w-12);h6_upload(&g_rahHeadTex[k],g_rahHeader,w,17);h6_tex_quad(g_rahHeadTex[k],w,17,xs[k],0,xs[k]+w*sc,17*sc);}
 glPopAttrib();g_sbDrawnHudPower=ph?status:-1;for(int k=0;k<3;k++)g_sbDrawnHudCount[k]=status==k?ph*4:0;
}
static void rah_free(void){g_rahCameos.clear();glDeleteTextures(1,&g_rahTex);glDeleteTextures(1,&g_rahWingTex);glDeleteTextures(4,g_rahHeadTex);g_rahTex=g_rahWingTex=0;memset(g_rahHeadTex,0,sizeof g_rahHeadTex);if(g_raHudPack)hud640_free(g_raHudPack);g_raHudPack=0;}
