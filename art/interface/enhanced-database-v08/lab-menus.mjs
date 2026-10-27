import {drawFactionBadge,loadFactionMeshes} from './faction-logos.mjs';
import {widePages,titles,controlDefaults,rangeValue} from './flows.mjs';
import {profile,key,value,defaults} from './visuals.mjs';
import {menuLayout,WHITE,MUTED,GREEN} from './layout.mjs';
const $=id=>document.getElementById(id),canvas=$('review-raster'),ctx=canvas.getContext('2d'),stage=$('stage'),hits=$('menu-hits');
let ready=false,badgeAngle=null;
let manifest,content={},history=[],assets={},page='main',section='display',parent='pause',focus=null,hover=null,pressed=null,layout,scale=2,origin=[0,0],settings={},openDrop=null,nodes=new Map();
const tintCache=new Map();
function tinted(id,col){const key=id+':'+col.join(',');if(tintCache.has(key))return tintCache.get(key);const src=assets[id],c=document.createElement('canvas');c.width=src.width;c.height=src.height;const cc=c.getContext('2d');cc.drawImage(src,0,0);const im=cc.getImageData(0,0,c.width,c.height);for(let n=0;n<im.data.length;n+=4){const shadow=im.data[n]<64&&im.data[n+1]<64&&im.data[n+2]<64;im.data.set(shadow?[0,0,0]:col,n);}cc.putImageData(im,0,0);tintCache.set(key,c);return c;}
const advances={};
function textW(value,font='font_menu'){const a=manifest.assets[font];return [...String(value)].reduce((v,c)=>v+(a.advance[c.charCodeAt(0)]||0),0)*(a.scale||1);}
function text(value,x,y,font='font_menu',col=WHITE,center=false){const a=manifest.assets[font],im=tinted(font,col),s=a.scale||1;if(center)x=Math.floor(x-textW(value,font)/2);for(const c of value){const n=c.charCodeAt(0)-(a.charStart||0);if(n<0||n>=a.frames)continue;ctx.drawImage(im,n*a.cellW,0,a.cellW,a.cellH,x,y,a.cellW*s,a.cellH*s);x+=a.advance[n]*s;}}
function fitted(value,width){let v=value;while(textW(v)>width&&v.length)v=v.slice(0,-1);return v===value?v:v.slice(0,-2)+'..';}
function note(n){const col=n.col??MUTED;if(n.center){text(n.text,n.x,n.y,'font_menu',col,true);return;}if(!n.wrap){text(n.text,n.x,n.y,'font_menu',col);return;}let lines=[];for(const paragraph of n.text.split('\n')){let line='';for(const word of paragraph.split(/\s+/)){if(textW((line?line+' ':'')+word)>n.wrap&&line){lines.push(line);line=word;}else line+=(line?' ':'')+word;}if(line)lines.push(line);}if(n.maxLines&&lines.length>n.maxLines){lines=lines.slice(-n.maxLines);if(n.maxLines===1)lines=[fitted(n.text,n.wrap)];}lines.forEach((v,i)=>text(fitted(v,n.wrap),n.x,n.y+i*(n.lineHeight??26),'font_menu',col));}
const playerColours=['#dfb54b','#db4540','#56baca','#dd9138','#58a354','#6381c6','#b778c5','#bcb9ad'];
function img(id,x,y){ctx.drawImage(assets[id],x,y);}
function change(next,push=true){if(push&&next!==page)history.push(page);page=next;parent=history.at(-1)??'main';focus=null;hover=null;pressed=null;openDrop=null;$('menu').value=next;render();}
function back(){change(history.pop()??'main',false);}
function confirm(title,message,actionName,returnTo){settings.confirmTitle=title;settings.confirmMessage=message;settings.confirmAction=actionName;settings.confirmReturn=returnTo;change('confirm');}
function action(e){
 if(!e||e.disabled)return;
 if(e.kind==='dbcat'||e.kind==='dbhouse'||e.kind==='dbdetail'){
  settings['database:'+({'dbcat':'cat','dbhouse':'house','dbdetail':'detail'}[e.kind])]=e.option;
  if(e.kind!=='dbdetail'){settings['database:top']=0;delete settings['database:selected'];}render();return;
 }
 if(e.kind==='nav'){change(e.route,false);return;}
 if(e.kind==='network'){settings.net=e.option;render();return;}
 if(e.kind==='filter'){settings[page+':filter']=e.option;settings[page+':top']=0;render();return;}
 if(e.kind==='page'){settings[page+':top']=(settings[page+':top']??0)+e.delta;render();const row=layout.entries.find(n=>n.kind==='row');if(row){settings[page+':selected']=row.option;render();}return;}
 if(e.kind==='row'){settings[page+':selected']=e.option;focus=e.id;render();return;}
 if(e.kind==='room'){settings.room=e.option;render();return;}
 if(e.kind==='seat'){settings.selectedSeat=e.option;settings.network=page==='lobby';change('seat');return;}
 if(e.kind==='colour'){
  const seat=settings.selectedSeat??0,k='seat'+seat+':colour',old=settings[k]??seat;
  for(let n=0;n<8;n++)if(n!==seat&&(settings['seat'+n+':colour']??n)===e.value)settings['seat'+n+':colour']=old;
  settings[k]=e.value;render();return;
 }
 if(e.kind==='campaign'){settings.campaignSide=e.option;$('status').textContent='Campaign preview: '+e.option+'.';focus=e.id;render();return;}
 if(e.kind==='radio'){settings[key(e.key,settings)]=e.option;focus=e.id;openDrop=null;render();return;}
 if(e.kind==='option'){
  if(page==='seat'){const slot={'Seat type':'mode','Faction':'side','Team':'team','Start position':'start'}[e.key];settings['seat'+(settings.selectedSeat??0)+':'+slot]=e.option;}
  else settings[e.local?e.key:key(e.key,settings)]=e.option;
  openDrop=null;focus=e.key;render();return;
 }
 if(e.kind==='drop'){openDrop=openDrop===e.id?null:e.id;focus=e.id;render();return;}
 if(e.kind==='check'){
  if(e.local){settings[e.id]=e.value!=='ON';if(e.id==='Internet Game'&&settings[e.id]){settings['Private Game']=true;settings['List Publicly']=false;}}
  else settings[key(e.id,settings)]=value(e.id,settings)==='ON'?'OFF':'ON';
  focus=e.id;render();return;
 }
 if(e.label==='Reset to Defaults'){
  if(page==='controls')for(const id of Object.keys(controlDefaults))delete settings[id];
  else{const mode=profile(settings);for(const k of Object.keys(settings))if(k.startsWith(mode+':')||['Display mode','Resolution','Vertical sync','FPS counter'].includes(k))delete settings[k];}
  openDrop=null;render();return;
 }
 if(e.tab){section=e.label.toLowerCase();openDrop=null;focus=e.id;render();return;}
 if(e.setting){settings[e.id]=(settings[e.id]??e.value)==='ON'?'OFF':'ON';focus=e.id;render();return;}
 const routes={'Visuals':'visuals','Game Controls':'controls','Gameplay':'gameplay','Save Mission':'save','Load Mission':'load','Special Ops':'special','User Maps':'usermaps','Skirmish':'skirmish','Multiplayer':'multiplayer','Start New Game':'campaign','Intro & Sneak Peek':'intro','Restate':'restate','Change Map':'maps','Set Up Match':'skirmish'};
 if(routes[e.label]){if(e.label==='Set Up Match')settings.setupOnly=true;if(e.label==='Skirmish')settings.setupOnly=false;change(routes[e.label]);return;}
 if(e.label==='Host'||e.label==='Join'){if(e.label==='Host'&&settings['Private Game']&&!/^\d{4}$/.test(settings.Passcode??'')){$('status').textContent='Enter a four digit passcode for a private game.';return;}settings.joined=e.label==='Join';settings.network=true;change('lobby');return;}
 if(e.label==='Use Settings'){settings.setupOnly=false;back();return;}
 if(e.label==='Done'&&['skirmishrules','lobbyrules'].includes(page)){change(page==='skirmishrules'?'skirmish':'lobby',false);return;}
 if(e.label==='Use Map'){if(layout.selection)settings.mapId=layout.selection.id;back();return;}
 if(e.label==='Ready'||e.label==='Not Ready'){settings['Local ready']=!settings['Local ready'];render();return;}
 if(e.label==='Send'){const message=settings.Message?.trim();if(message){settings.messages=[...(settings.messages??['Lobby: Welcome, Commander.','Commander: Ready when you are.']),(settings['Player name']??'Commander')+': '+message];settings.Message='';}render();return;}
 if(page==='confirm'&&e.label===settings.confirmAction){$('status').textContent='Preview selection: '+e.label+'.';change(settings.confirmReturn??'main',false);history=[];return;}
 if(e.label==='Exit Game'){confirm('EXIT GAME','Exit OpenCNC3D?','Exit Game','main');return;}
 if(e.label==='Abort Mission'){confirm('ABORT MISSION','Abort this mission and return to the main menu?','Abort Mission','main');return;}
 if(e.label==='Delete Mission'){confirm('DELETE MISSION','Delete the selected saved mission?','Delete Mission','pause');return;}
 if(['Back','OK','Done','Cancel','Leave','Close'].includes(e.label)){if(openDrop){openDrop=null;render();}else back();return;}
 if(e.label==='Play Intro'){$('status').textContent='Original INTRO2 movie playback is represented by a still in this Lab review.';return;}
 if(e.label==='Resume Mission'){change('main',false);history=[];return;}
 $('status').textContent='Preview selection: '+e.label+'.';focus=e.id;render();
}
function reconcile(){
 const live=new Set();
 for(const e of layout.entries){
  live.add(e.id);let b=nodes.get(e.id);
  if(!b){
   b=document.createElement(e.kind==='range'||e.kind==='field'?'input':'button');
   if(e.kind==='range'){b.type='range';b.addEventListener('input',()=>{settings[e.id]=Number(b.value);focus=e.id;render();});}
   else if(e.kind==='field'){b.type='text';b.addEventListener('input',()=>{if(e.password)b.value=b.value.replace(/[^0-9]/g,'');settings[e.id]=b.value;focus=e.id;render();});}
   else{b.textContent=e.label;b.addEventListener('click',()=>action(layout.entries.find(n=>n.id===b.dataset.id)));}
   b.dataset.id=e.id;nodes.set(e.id,b);hits.append(b);
   b.addEventListener('pointerenter',()=>{hover=e.id;render();});b.addEventListener('pointerleave',()=>{if(hover===e.id)hover=null;pressed=null;render();});b.addEventListener('pointerdown',()=>{pressed=e.id;render();});b.addEventListener('pointerup',()=>{pressed=null;render();});b.addEventListener('focus',()=>{focus=e.id;render();});
   b.addEventListener('keydown',ev=>{
    if(b.tagName==='BUTTON'&&['ArrowDown','ArrowUp'].includes(ev.key)){ev.preventDefault();const list=layout.entries.filter(n=>!n.disabled),i=list.findIndex(n=>n.id===b.dataset.id);nodes.get(list[(i+(ev.key==='ArrowDown'?1:list.length-1))%list.length].id).focus();}
    if(ev.key==='Escape'){ev.preventDefault();if(openDrop){openDrop=null;render();}else back();}
    if(ev.key==='Enter'&&e.kind==='field'&&page==='lobby'){ev.preventDefault();action(layout.entries.find(n=>n.label==='Send'));}
   });
  }
  b.hidden=false;b.disabled=!!e.disabled||!!(openDrop&&!e.overlay&&e.id!==openDrop);
  for(const a of ['aria-pressed','aria-checked','aria-selected','aria-expanded','aria-valuenow','aria-valuetext'])b.removeAttribute(a);
  b.setAttribute('aria-label',e.label);
  if(e.kind==='range'){b.removeAttribute('role');b.min=e.min;b.max=e.max;b.step=e.step;b.value=e.value;b.setAttribute('aria-valuetext',rangeValue(e));b.style.setProperty('--thumb',28*scale+'px');}
  else if(e.kind==='field'){b.removeAttribute('role');b.maxLength=e.maxLength;b.value=e.value;}
  else{
   b.setAttribute('role',e.kind==='check'?'checkbox':e.kind==='radio'?'radio':e.tab||e.kind==='filter'||e.kind==='nav'||e.kind==='network'?'tab':'button');
   if(e.kind==='check')b.setAttribute('aria-checked',String(e.value==='ON'));else if(e.kind==='radio')b.setAttribute('aria-checked',String(!!e.active));else if(e.tab||['filter','nav','network','dbcat','dbhouse','dbdetail'].includes(e.kind))b.setAttribute('aria-selected',String(!!e.active));else if(e.kind==='drop'){b.setAttribute('aria-expanded',String(openDrop===e.id));b.setAttribute('aria-label',e.label+': '+e.value);}else if(['row','seat','room'].includes(e.kind))b.setAttribute('aria-pressed',String(!!e.active));
  }
  b.style.zIndex=e.overlay?'2':'1';Object.assign(b.style,{left:(origin[0]+e.x*scale)+'px',top:(origin[1]+e.y*scale)+'px',width:e.w*scale+'px',height:e.h*scale+'px'});
 }
 for(const [id,b]of nodes)if(!live.has(id))b.hidden=true;
}
let evaMask;
function stamp(){
 if(!evaMask){
  const source=assets.eva,c=document.createElement('canvas');c.width=source.width;c.height=source.height;const cc=c.getContext('2d');cc.drawImage(source,0,0);const pixels=cc.getImageData(0,0,c.width,c.height).data;
  const mask=document.createElement('canvas');mask.width=80;mask.height=30;const mc=mask.getContext('2d'),ink=mc.createImageData(80,30);
  // White monogram / light-grey background are explicit roles in the supplied flat logo.
  // Preserve fractional source coverage at the contour instead of a binary stair-step.
  // Keep the panel grain and hard recess walls; only the silhouette edge is softened.
  for(let y=0;y<30;y++)for(let x=0;x<80;x++){
   let coverage=0,total=0;
   for(let sy=Math.floor(y*c.height/30);sy<Math.floor((y+1)*c.height/30);sy++)for(let sx=Math.floor(x*c.width/80);sx<Math.floor((x+1)*c.width/80);sx++){
    const i=(sy*c.width+sx)*4;total++;if(pixels[i]>245&&pixels[i+1]>245&&pixels[i+2]>245)coverage++;
   }
   ink.data.set([255,255,255,Math.round(255*coverage/total)],(y*80+x)*4);
  }
  mc.putImageData(ink,0,0);evaMask=mask;
 }
 // Carve the mark into the existing panel; its grain remains the recessed floor.
 ctx.globalAlpha=.34;const floor=document.createElement('canvas');floor.width=80;floor.height=30;const fc=floor.getContext('2d');fc.drawImage(evaMask,0,0);fc.globalCompositeOperation='source-in';fc.fillStyle='#000';fc.fillRect(0,0,80,30);ctx.drawImage(floor,32,24);
 const wall=(dx,dy,colour,opacity)=>{
  const layer=document.createElement('canvas');layer.width=80;layer.height=30;const lc=layer.getContext('2d');lc.drawImage(evaMask,0,0);lc.globalCompositeOperation='destination-out';lc.drawImage(evaMask,dx,dy);lc.globalCompositeOperation='source-in';lc.fillStyle=colour;lc.fillRect(0,0,80,30);ctx.globalAlpha=opacity;ctx.drawImage(layer,32,24);
 };
 // Recess walls are INSIDE the original silhouette: dark upper-left, lit lower-right.
 wall(2,2,'#000',.9);wall(-1,-1,'#858b7d',.68);ctx.globalAlpha=1;
}
function well(g){
 // A cut-corner inset liner, with the panel's continuous material visible inside.
 const {x,y,w,h}=g;if(g.overlay){ctx.fillStyle='#101310';ctx.beginPath();ctx.moveTo(x+4,y);ctx.lineTo(x+w-4,y);ctx.lineTo(x+w,y+4);ctx.lineTo(x+w,y+h-4);ctx.lineTo(x+w-4,y+h);ctx.lineTo(x+4,y+h);ctx.lineTo(x,y+h-4);ctx.lineTo(x,y+4);ctx.closePath();ctx.fill();}
 ctx.strokeStyle='#080a09';ctx.lineWidth=2;ctx.beginPath();ctx.moveTo(x+4,y+1);ctx.lineTo(x+w-5,y+1);ctx.lineTo(x+w-1,y+5);ctx.lineTo(x+w-1,y+h-5);ctx.lineTo(x+w-5,y+h-1);ctx.lineTo(x+4,y+h-1);ctx.lineTo(x+1,y+h-5);ctx.lineTo(x+1,y+5);ctx.closePath();ctx.stroke();
 ctx.fillStyle='#4a4b42';ctx.fillRect(x+4,y+h-2,w-8,1);ctx.fillRect(x+w-2,y+5,1,h-10);
 if(g.title){text(g.title,x+18,y+14,'font_menu',GREEN);ctx.fillStyle='#0b0d0b';ctx.fillRect(x+16,y+40,w-32,1);ctx.fillStyle='#353930';ctx.fillRect(x+16,y+41,w-32,1);}
}
function control(e){
 if(e.kind==='range'){
  text(e.label,e.x,e.y-28);const v=rangeValue(e);text(v,e.x+e.w-textW(v),e.y-28,'font_menu',GREEN);
  const x=e.x+14,y=e.y+14,w=e.w-28,fill=Math.round(w*(e.value-e.min)/(e.max-e.min));
  ctx.fillStyle='#080a08';ctx.fillRect(x-2,y-2,w+4,12);ctx.fillStyle='#55574c';ctx.fillRect(x,y+9,w,1);ctx.fillRect(x+w+1,y,w?1:0,9);ctx.fillStyle='#1b2618';ctx.fillRect(x,y,w,8);
  ctx.fillStyle=hover===e.id||focus===e.id?'#7ddb45':'#527d35';ctx.fillRect(x,y,fill,8);ctx.fillStyle='#a5c777';ctx.fillRect(x,y,fill,1);
  img('button-28-28-'+(pressed===e.id?'pressed':hover===e.id||focus===e.id?'hover':'normal'),e.x+fill,e.y+4);return;
 }
 if(e.kind==='field'){
  text(e.label,e.labelX,e.labelY,'font_menu',e.disabled?MUTED:WHITE);ctx.fillStyle='#080a09';ctx.fillRect(e.x,e.y,e.w,e.h);ctx.fillStyle=focus===e.id?'#b19a63':'#606255';ctx.fillRect(e.x,e.y,e.w,1);ctx.fillRect(e.x,e.y,1,e.h);ctx.fillStyle='#353b30';ctx.fillRect(e.x,e.y+e.h-1,e.w,1);ctx.fillRect(e.x+e.w-1,e.y,1,e.h);
  const v=e.password?'*'.repeat(e.value.length):e.value;text(fitted(v,e.w-24),e.x+12,e.y+12,'font_menu',e.disabled?MUTED:WHITE);if(focus===e.id&&!e.disabled){ctx.fillStyle='#b5cf91';ctx.fillRect(Math.min(e.x+12+textW(v),e.x+e.w-14),e.y+10,1,19);}return;
 }
 if(['row','seat','room'].includes(e.kind)){
  const on=e.active||focus===e.id||hover===e.id;if(on){ctx.fillStyle=e.active?'#273521':'#30342b';ctx.fillRect(e.x,e.y,e.w,e.h);ctx.fillStyle='#637450';ctx.fillRect(e.x,e.y,2,e.h);}
  let left=e.x+12;if(e.badge){drawFactionBadge(ctx,e.badge,e.x+6,e.y+2,28,badgeAngle??(window.matchMedia('(prefers-reduced-motion: reduce)').matches?.25:performance.now()*.0002));left=e.x+44;}if(e.kind==='seat'){ctx.fillStyle=playerColours[settings['seat'+e.option+':colour']??e.colour];ctx.fillRect(e.x+8,e.y+9,12,12);left=e.x+32;}
  text(fitted(e.label,e.summary?e.kind==='seat'?Math.floor(e.w*.46)-44:230:e.w-(e.badge?56:28)),left,e.y+Math.floor((e.h-16)/2),'font_menu',e.grey?MUTED:WHITE);if(e.summary)text(fitted(e.summary,e.kind==='seat'?e.w-Math.floor(e.w*.46)-12:208),e.x+(e.kind==='seat'?Math.floor(e.w*.46):246),e.y+Math.floor((e.h-16)/2),'font_menu',MUTED);if(e.tail)text(e.tail,e.x+478,e.y+14,'font_menu',MUTED);return;
 }
 if(e.kind==='colour'){img('button-28-28-'+(e.active?'active':hover===e.id?'hover':'normal'),e.x,e.y);ctx.fillStyle=playerColours[e.value];ctx.fillRect(e.x+7,e.y+7,14,14);return;}

 const selected=e.active||(!focus&&e.primary),active=e.kind==='radio'||e.tab||['filter','nav','network','dbcat','dbhouse','dbdetail'].includes(e.kind)?selected:selected||focus===e.id;
 const state=e.disabled?'disabled':pressed===e.id?'pressed':hover===e.id?(active?'activehover':'hover'):active?'active':'normal';
 if(e.kind==='check'){
  if(hover===e.id||focus===e.id){ctx.fillStyle='#353c30';ctx.fillRect(e.x,e.y,e.w,e.h);}
  const x=e.x+2,y=e.y+7;ctx.fillStyle='#787b6b';ctx.fillRect(x,y,22,22);ctx.fillStyle='#10130f';ctx.fillRect(x+1,y+1,20,20);ctx.fillStyle='#373b32';ctx.fillRect(x+2,y+20,18,1);ctx.fillRect(x+20,y+2,1,18);
  if(e.value==='ON'){ctx.fillStyle=e.disabled?'#797f73':'#72df4c';[[5,10],[7,12],[9,14],[11,12],[13,10],[15,8],[17,6]].forEach(([a,b])=>ctx.fillRect(x+a-1,y+b-1,3,3));}
  text(e.label,e.x+38,e.y+10,'font_menu',e.disabled?MUTED:WHITE);return;
 }
 img(`button-${e.w}-${e.h}-${state}`,e.x,e.y);
 if(e.badge)drawFactionBadge(ctx,e.badge,e.x+6,e.y+2,28,badgeAngle??performance.now()*.0002);
 if(e.kind==='drop'&&e.local||e.setting&&e.kind!=='radio'&&e.labelX!==undefined)text(e.label,e.labelX,e.labelY,'font_menu',e.disabled?MUTED:WHITE);
 const label=e.kind==='radio'||e.kind==='option'?e.label:e.kind==='drop'?e.value:e.setting?(page==='visuals'?e.value:settings[e.id]??e.value):e.label;
 const y=e.y+Math.floor((e.h-16)/2)+(pressed===e.id?1:0);
 text(label,e.x+e.w/2+(e.badge?14:0)-(e.kind==='drop'?8:0),y,'font_menu',e.disabled?MUTED:WHITE,true);
 if(e.kind==='drop'){ctx.fillStyle=e.disabled?'#90988b':'#e6e5da';const x=e.x+e.w-27,z=e.y+17;for(let i=0;i<5;i++)ctx.fillRect(x+i,z+i,9-2*i,1);}
}
function render(){if(!ready)return;const [w,h]=$('viewport').value.split(',').map(Number);canvas.width=w;canvas.height=h;stage.style.width=w+'px';stage.style.height=h+'px';ctx.imageSmoothingEnabled=false;ctx.fillStyle='#000';ctx.fillRect(0,0,w,h);layout=menuLayout(page,manifest,section,settings,openDrop,content);scale=Math.max(1,Math.floor(h/693));while(scale>1&&layout.size[0]*scale>w-40)scale--;
 if(page==='main'){
  const bg=assets['title-backdrop'],s=Math.max(1,Math.floor(Math.min(w/bg.width,h/bg.height)));ctx.drawImage(bg,Math.floor((w-bg.width*s)/2),Math.floor((h-bg.height*s)/2),bg.width*s,bg.height*s);ctx.fillStyle='#0002';ctx.fillRect(0,0,w,h);
 }else{const bg=assets.gameplay,s=Math.max(1,Math.floor(Math.min(w/bg.width,h/bg.height)));ctx.drawImage(bg,Math.floor((w-bg.width*s)/2),Math.floor((h-bg.height*s)/2),bg.width*s,bg.height*s);ctx.fillStyle='rgba(0,0,0,.3)';ctx.fillRect(0,0,w,h);}
 origin=[Math.floor((w-layout.size[0]*scale)/2),page==='main'?Math.floor((h-layout.size[1]*scale)/2)+52*scale:Math.floor((h-layout.size[1]*scale)/2)];
 if(page==='main'){const logo=assets.wordmark;ctx.drawImage(logo,Math.floor((w-logo.width*scale*2)/2),Math.max(0,origin[1]-logo.height*scale*2-16*scale),logo.width*scale*2,logo.height*scale*2);}
 ctx.save();ctx.translate(...origin);ctx.scale(scale,scale);ctx.imageSmoothingEnabled=false;
 const frame=['lobby','database'].includes(page)?'lobby-frame':page==='confirm'?'confirm-frame':page==='main'?'main-frame':page==='pause'?'pause-frame':page==='visuals'||widePages.has(page)?'visuals-frame':'settings-frame';ctx.shadowColor='rgba(0,0,0,.7)';ctx.shadowBlur=8;ctx.shadowOffsetX=2;ctx.shadowOffsetY=4;img(frame,0,0);ctx.shadowColor='transparent';ctx.shadowBlur=0;ctx.shadowOffsetX=0;ctx.shadowOffsetY=0;stamp();
 text((page==='confirm'?settings.confirmTitle??'EXIT GAME':titles[page])??(page==='main'?'MAIN MENU':page==='pause'?'OPTIONS':page==='visuals'?'VISUALS':page==='gameplay'?'GAMEPLAY':page==='save'?'SAVE MISSION':'LOAD MISSION'),layout.size[0]/2,32,'font_menu',WHITE,true);
 if(page==='pause'){text('MISSION PAUSED',layout.size[0]/2,64,'font_menu',GREEN,true);text('MISSION',36,158,'font_menu',MUTED);text('SETTINGS',362,158,'font_menu',MUTED);}
 if(page==='main'){text('ENHANCED  /  '+manifest.release,layout.size[0]/2,488,'font_menu',GREEN,true);}
 if(page==='visuals'){const mode=profile(settings).toUpperCase();text(mode,978-textW(mode),36,'font_menu',GREEN);}
 for(const g of (layout.groups??[]).filter(g=>!g.overlay))well(g);
 for(const n of layout.notes??[])note(n);
 for(const a of layout.art??[])ctx.drawImage(assets[a.id],a.x,a.y,a.w,a.h);
 for(const e of layout.entries.filter(e=>!e.overlay))control(e);
 for(const g of (layout.groups??[]).filter(g=>g.overlay))well(g);
 for(const e of layout.entries.filter(e=>e.overlay))control(e);
 ctx.restore();
 if(page==='main'){
  const size=Math.min(200,Math.max(0,Math.floor((w-layout.size[0]*scale)/2/scale)-36));
  if(size>=80){const native=size*scale,y=h-native-36*scale,margin=(w-layout.size[0]*scale)/4,angle=badgeAngle??performance.now()*.0002;
   drawFactionBadge(ctx,'GDI',margin-native/2,y,native,angle);drawFactionBadge(ctx,'Nod',w-margin-native/2,y,native,angle);
  }
 }
 reconcile();const failures=[];
 for(const e of layout.entries){if(e.x<13||e.y<28||e.x+e.w>layout.size[0]-13||e.y+e.h>layout.size[1]-32)failures.push(e.id+' leaves safe well');if(!['range','field','row','seat','room','colour'].includes(e.kind)&&textW(e.kind==='check'?e.label:e.kind==='radio'?e.label:e.value??e.label)>e.w-(e.kind==='check'?48:e.kind==='drop'?44:20))failures.push(e.id+' exceeds text safe width');}
 $('diagnostics').textContent=JSON.stringify({revision:manifest.version,page,section,nativePanel:layout.size,integerScale:scale,controls:layout.entries.length,preflightFailures:failures},null,2);if(failures.length)throw Error(failures.join('; '));document.body.dataset.ready='true';document.body.dataset.page=page;
}
$('menu').addEventListener('change',()=>{history=[$('menu').value==='controls'||$('menu').value==='gameplay'||$('menu').value==='visuals'?'pause':'main'];change($('menu').value,false);});$('viewport').addEventListener('change',render);$('full').addEventListener('click',()=>document.body.classList.toggle('full'));
$('capture').addEventListener('click',async()=>{if(!ready)return;badgeAngle=.25;render();const name='enhanced-menu-v07-'+page+(page==='special'?'-'+(settings['special:filter']??'Covert Ops').replaceAll(' ',''):'')+(page==='visuals'?'-'+section:'')+'-'+canvas.height;const url=canvas.toDataURL('image/jpeg',.98);badgeAngle=null;const response=await fetch('/api/shot',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({name,url})});const result=await response.json();if(!response.ok)throw Error(JSON.stringify(result));$('status').textContent='Saved screenshot: '+name+'.jpg';});
async function init(){await loadFactionMeshes();manifest=await(await fetch('manifest.json')).json();content=await(await fetch('menu-content.json')).json();content.database=(await(await fetch('database-content.json')).json()).rows;await Promise.all(Object.entries(manifest.assets).map(async([id,a])=>{const response=await fetch(a.file),bytes=await response.arrayBuffer(),digest=[...new Uint8Array(await crypto.subtle.digest('SHA-256',bytes))].map(n=>n.toString(16).padStart(2,'0')).join('');if(digest!==a.sha256)throw Error('Artwork hash mismatch: '+id);assets[id]=await createImageBitmap(new Blob([bytes]));}));ready=true;$('status').textContent='Enhanced menu review v08. Website faction logos and actual Enhanced Database model captures.';render();}
init().catch(e=>{$('status').textContent='Preflight error: '+e.message;document.body.dataset.error=e.message;console.error(e);});

let lastBadgeFrame=0;function animateBadges(now){if(ready&&['special','main','database'].includes(page)&&!document.hidden&&now-lastBadgeFrame>=50&&!window.matchMedia('(prefers-reduced-motion: reduce)').matches){lastBadgeFrame=now;render();}requestAnimationFrame(animateBadges);}requestAnimationFrame(animateBadges);
