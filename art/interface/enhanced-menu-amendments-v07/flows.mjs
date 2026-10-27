// Enhanced layouts use actual installed menu content; network/saves are local review fixtures.
export const widePages=new Set(['controls','gameplay','save','load','special','usermaps','skirmish','skirmishrules','maps','multiplayer','join','lobby','lobbyrules']);
export const titles={controls:'GAME CONTROLS',gameplay:'GAMEPLAY',save:'SAVE MISSION',load:'LOAD MISSION',special:'SPECIAL OPS',usermaps:'USER MAPS',skirmish:'SKIRMISH',skirmishrules:'SKIRMISH',maps:'CHANGE MAP',campaign:'CHOOSE YOUR SIDE',multiplayer:'MULTIPLAYER',join:'MULTIPLAYER',lobby:'MULTIPLAYER LOBBY',lobbyrules:'MULTIPLAYER LOBBY',seat:'PLAYER SETUP',intro:'INTRO & SNEAK PEEK',restate:'MISSION BRIEFING',confirm:'CONFIRM'};
export const controlDefaults={'Game speed':4,'Scroll rate':3,'Music volume':167,'Sound volume':215,'Speech volume':239};
export const rangeValue=e=>e.percent?Math.round(100*e.value/e.max)+'%':e.id==='Game speed'||e.id==='Scroll rate'?['Slowest','Slower','Slow','Normal','Fast','Faster','Fastest'][e.value]:String(e.value);
export function flowLayout(page,s,content,openDrop=null){
 const wide=widePages.has(page),size=page==='confirm'?[676,340]:page==='lobby'?[1216,636]:wide?[1016,636]:[676,584],entries=[],groups=[],notes=[],art=[];
 const add=(label,x,y,w=192,h=40,extra={})=>entries.push({id:label,label,x,y,w,h,...extra});
 const note=(text,x,y,col)=>notes.push({text,x,y,col});
 const group=(title,x,y,w,h)=>groups.push({title,x,y,w,h});
 const check=(id,x,y,defaultOn=false,disabled=false)=>add(id,x,y,424,36,{kind:'check',value:(s[id]??defaultOn)?'ON':'OFF',local:true,disabled});
 const field=(id,x,y,w,defaultValue='',maxLength=32,disabled=false,password=false)=>add(id,x,y,w,40,{kind:'field',value:s[id]??defaultValue,maxLength,disabled,password,labelX:x,labelY:y-24});
 const drop=(id,x,y,w,options,defaultValue=options[0])=>add(id,x,y,w,40,{kind:'drop',key:id,local:true,options,value:s[id]??defaultValue,labelX:x,labelY:y-24});
 const range=(id,x,y,w,min,max,step,def,percent=false)=>add(id,x,y,w,36,{kind:'range',min,max,step,value:s[id]??def,percent,labelX:x,labelY:y-28});
 const footer=(primary='OK',back='Back')=>{add(back,38,size[1]-86,192,40);add(primary,size[0]-258,size[1]-86,220,40,{primary:true});};
 const selectedMap=content.maps.find(m=>m.id===s.mapId)??content.maps[0];
 const preview=(m,x,y)=>{if(m&&content.assets['map-'+m.id])art.push({id:'map-'+m.id,x,y,w:240,h:240});else note('No preview available',x,y+98);};
 if(page==='controls'){
  group('GAMEPLAY',38,110,460,408);group('AUDIO',518,110,460,408);
  range('Game speed',56,216,424,0,6,1,controlDefaults['Game speed']);range('Scroll rate',56,354,424,0,6,1,controlDefaults['Scroll rate']);
  note('Slower',56,264);note('Faster',414,264);note('Slower',56,402);note('Faster',414,402);
  ['Music volume','Sound volume','Speech volume'].forEach((id,i)=>range(id,536,216+i*100,424,0,239,1,controlDefaults[id],true));
  add('Reset to Defaults',38,550,292);add('Back',550,550);add('OK',758,550,220,40,{primary:true});
 }else if(page==='gameplay'){
  group('INPUT',38,110,460,408);group('FEEDBACK',518,110,460,408);
  check('Swap mouse buttons',56,202);check('Right button scrolls',56,266,true);check('Credit tick sound',536,202,true);footer();
 }else if(page==='save'||page==='load'){
  group('SAVED MISSIONS',38,110,600,408);group('MISSION',658,110,320,408);
  const saves=[['save-0','GDI - Beachhead','GDI'],['save-1','Nod - Liberation','Nod'],['save-2','GDI - Refinery','GDI']],sel=saves.find(v=>v[0]===s[page+':selected'])??saves[0];
  saves.forEach((v,i)=>add(v[1],56,178+i*54,564,48,{id:'row:'+v[0],kind:'row',option:v[0],active:v===sel}));
  art.push({id:'faction-'+sel[2],x:750,y:186,w:136,h:120});field('Mission name',676,398,284,sel[1],31,page==='load');note(sel[2],676,460);footer(page==='save'?'Save':'Load');
 }else if(['special','usermaps','maps'].includes(page)){
  const list=page==='special'?content.special:page==='usermaps'?content.usermaps:content.maps;
  const tabs=page==='special'?['Covert Ops','Special Ops']:page==='maps'?['Official','User Maps']:[];
  const filter=s[page+':filter']??tabs[0],filtered=page==='special'?list.filter(n=>filter==='Covert Ops'?n.group.includes('Covert'):n.group.includes('Special Ops')).sort((a,b)=>(a.faction==='Nod')-(b.faction==='Nod')):tabs.length?list.filter(n=>n.group===filter):list;
  tabs.forEach((t,i)=>add(t,38+i*238,86,230,40,{id:'filter:'+t,kind:'filter',option:t,active:t===filter}));
  const y=tabs.length?150:110,height=tabs.length?368:408;group(page==='maps'?'MAPS':'MISSIONS',38,y,600,height);group('DETAILS',658,y,320,height);
  const top=Math.max(0,Math.min(s[page+':top']??0,Math.max(0,filtered.length-8))),sel=filtered.find(n=>n.id===s[page+':selected'])??filtered[0];
  filtered.slice(top,top+8).forEach((m,i)=>add(m.name,56,y+48+i*34,564,32,{id:'row:'+m.id,kind:'row',option:m.id,active:m.id===sel?.id,badge:page==='special'?m.faction:null}));
  if(!filtered.length)note(page==='maps'?'No maps installed':'No missions installed',56,y+64);
  note(filtered.length+' '+(page==='maps'?'maps':'missions'),56,490);
  if(filtered.length>8){add('Previous',346,474,132,32,{disabled:top===0,kind:'page',delta:-8});add('Next',488,474,132,32,{disabled:top+8>=filtered.length,kind:'page',delta:8});}
  if(sel){
   if(page==='maps'){preview(sel,698,204);note(sel.starts+' players / '+sel.width+' X '+sel.height,676,460);note(sel.theater,676,486);}
   else{art.push({id:'faction-'+sel.faction,x:742,y:212,w:136,h:120});note(sel.faction,676,370);note(sel.id,676,404);note(sel.theater+' / '+sel.width+' X '+sel.height,676,438);}
  }
  footer(page==='maps'?'Use Map':'Play');entries.at(-1).disabled=!sel;
  return finish({selection:sel});
 }else if(page==='skirmish'||page==='lobby'){
  const network=page==='lobby';
  add('Players & Map',38,86,230,40,{kind:'nav',route:page,active:true});add('Match Rules',276,86,230,40,{kind:'nav',route:network?'lobbyrules':'skirmishrules'});if(network)add('Change Map',514,86,230);
  group('PLAYERS',38,150,network?392:600,368);
  group('MAP',network?450:658,150,network?188:320,368);
  note('Player',56,194);note('Side / Team / Start',network?206:340,194);
  const seats=Math.min(8,selectedMap?.starts??2);
  for(let i=0;i<seats;i++){
   const prefix='seat'+i+':',mode=s[prefix+'mode']??(i===0?'Player':network?'Empty':'Computer'),side=s[prefix+'side']??(i%2?'Nod':'GDI');
   add(i===0?(s['Player name']??'Commander'):mode==='Computer'?'Computer '+i:mode,56,222+i*32,network?356:564,30,{id:'seat:'+i,kind:'seat',option:i,colour:i,summary:mode==='Empty'||mode==='Blocked'?'':side+' / '+(s[prefix+'team']??String(i+1))+' / '+(s[prefix+'start']??'Random')});
  }
  if(network){
   if(selectedMap&&content.assets['map-'+selectedMap.id])art.push({id:'map-'+selectedMap.id,x:484,y:204,w:120,h:120});else notes.push({text:'No preview',x:468,y:240,wrap:152,maxLines:2});
   notes.push({text:selectedMap?.name??'No map selected',x:468,y:348,wrap:152,maxLines:2});
   note((selectedMap?.starts??0)+' players',468,412);note(selectedMap?.starts?'Starts 1 - '+selectedMap.starts:'No starts',468,444);note((selectedMap?.width??0)+' X '+(selectedMap?.height??0),468,476);
   group('CHAT',658,150,520,368);notes.push({text:(s.messages??['Lobby: Welcome, Commander.','Commander: Ready when you are.']).join('\n'),x:676,y:194,wrap:484,maxLines:11,lineHeight:24});field('Message',676,466,392,'',72);delete entries.at(-1).labelX;add('Send',1080,466,80);
  }else{preview(selectedMap,698,202);note(selectedMap?.name??'No map selected',676,460);note((selectedMap?.starts??0)+' players / '+(selectedMap?.width??0)+' X '+(selectedMap?.height??0),676,486);add('Change Map',550,550);}
  add(network?'Leave':'Back',38,550);add(network?(s.joined?(s['Local ready']?'Not Ready':'Ready'):'Start Game'):s.setupOnly?'Use Settings':'Play',network?958:758,550,220,40,{primary:true,disabled:network&&!s.joined&&!s['All players ready']});
 }else if(page==='skirmishrules'||page==='lobbyrules'){
  const network=page==='lobbyrules';add('Players & Map',38,86,230,40,{kind:'nav',route:network?'lobby':'skirmish'});add('Match Rules',276,86,230,40,{kind:'nav',route:page,active:true});if(network)add('Change Map',514,86,230);
  group('STARTING CONDITIONS',38,150,460,368);group('MATCH RULES',518,150,460,368);
  range('Tech Level',56,242,424,1,7,1,7);range('Credits',56,344,424,0,10000,500,5000);range('Unit Count',56,446,424,0,10,1,1);
  ['Bases','Tiberium Regrows','Superweapons','Crates','AI Takeover','Short Game'].forEach((id,i)=>check(id,536,202+i*48,i<3,i<2));
  footer('Done');
 }else if(page==='seat'){
  const i=s.selectedSeat??0,prefix='seat'+i+':';group('PLAYER '+(i+1),38,102,600,356);
  drop('Seat type',56,178,564,i===0?['Player']:['Computer','Empty','Blocked'],s[prefix+'mode']??(i===0?'Player':s.network?'Empty':'Computer'));drop('Faction',56,270,268,['GDI','Nod'],s[prefix+'side']??(i%2?'Nod':'GDI'));
  // Colour remains a swatch; original engine colours have no invented names.
  note('Player colour',352,246);for(let n=0;n<8;n++)add('Colour '+n,352+n*32,270,28,28,{id:'colour:'+n,kind:'colour',value:n,active:(s[prefix+'colour']??i)===n});
  drop('Team',56,364,268,['1','2','3','4','5','6','7','8'],s[prefix+'team']??String(i+1));drop('Start position',352,364,268,['Random',...Array.from({length:selectedMap?.starts??2},(_,n)=>String(n+1))],s[prefix+'start']??'Random');footer('Done');
 }else if(page==='campaign'){
  group('CAMPAIGN',38,110,600,350);
  art.push({id:'faction-GDI',x:120,y:196,w:136,h:120},{id:'faction-Nod',x:420,y:196,w:136,h:120});
  add('GDI',56,356,268,40,{kind:'campaign',option:'GDI',active:s.campaignSide==='GDI'});add('Nod',352,356,268,40,{kind:'campaign',option:'Nod',active:s.campaignSide==='Nod'});add('Back',38,498,192);
 }else if(page==='multiplayer'){
  add('Host Game',38,86,230,40,{kind:'nav',route:'multiplayer',active:true});add('Join Game',276,86,230,40,{kind:'nav',route:'join'});
  group('GAME',38,150,460,368);group('ACCESS',518,150,460,368);
  field('Game name',56,232,424,"Commander's Game",31);field('Player name',56,330,424,'Commander',11);add('Set Up Match',56,422,424);note(selectedMap?.name??'Choose a map',56,478);
  check('Private Game',536,204);field('Passcode',536,294,424,'',4,!(s['Private Game']??false),true);
  check('Internet Game',536,356);check('List Publicly',536,414,false,!!s['Internet Game']);note(s['Internet Game']?'Join using a room code':'Public listing shares your address',536,474);
  footer('Host');
 }else if(page==='join'){
  add('Host Game',38,86,230,40,{kind:'nav',route:'multiplayer'});add('Join Game',276,86,230,40,{kind:'nav',route:'join',active:true});
  add('LAN',514,86,140,40,{kind:'network',option:'LAN',active:(s.net??'LAN')==='LAN'});add('Internet',662,86,140,40,{kind:'network',option:'Internet',active:s.net==='Internet'});
  group('AVAILABLE GAMES',38,150,600,368);group('JOIN DETAILS',658,150,320,368);
  note('Game',56,200);note('Map',302,200);note('Players',534,200);
  const rooms=[['EVA training','GREEN ACRES','2 / 8',0,false,false],['Private game','SAND TRAP','1 / 8',1,true,false],['Full game','LOST ARENA','8 / 8',2,false,true]].filter(r=>(!s['Hide Locked']||!r[4])&&(!s['Hide Greyed']||!r[5]));const selectedRoom=rooms.find(r=>r[3]===(s.room??0));
  rooms.forEach((r,i)=>add(r[0],56,240+i*48,564,44,{id:'room:'+r[3],kind:'room',option:r[3],active:(s.room??0)===r[3],summary:r[1],tail:r[2],grey:r[5]}));
  check('Hide Locked',56,408);check('Hide Greyed',56,454);
  field('Player name',676,238,284,'Commander',11);field(s.net==='Internet'?'Room code':'Host address',676,336,284,'',63);field('Join passcode',676,434,284,'',4,!selectedRoom?.[4],true);
  add('Refresh',38,550,192);add('Back',550,550);add('Join',758,550,220,40,{primary:true,disabled:!selectedRoom||selectedRoom[5]});
 }else if(page==='intro'){
  group('MOVIES',38,110,600,350);art.push({id:'intro-frame',x:178,y:174,w:320,h:156});add('Play Intro',56,382,564);add('Back',38,498,192);
 }else if(page==='restate'){
  group('MISSION OBJECTIVE',38,110,600,350);notes.push({text:content.missionBriefing||'Mission briefing is not available.',x:56,y:178,wrap:564});add('Back',38,498,192);
 }else if(page==='confirm'){
  notes.push({text:s.confirmMessage??'Exit OpenCNC3D?',x:338,y:136,center:true});
  add('Cancel',98,238,220);add(s.confirmAction??'Exit Game',358,238,220,40,{primary:true});

 }
 return finish();
 function finish(extra={}){
  if(openDrop){const owner=entries.find(e=>e.id===openDrop&&!e.disabled);if(owner){let height=owner.options.length*42+8,y=owner.y+owner.h+4;if(y+height>size[1]-98)y=Math.max(78,owner.y-height-4);groups.push({x:owner.x-4,y:y-4,w:owner.w+8,h:height,overlay:true});owner.options.forEach((t,i)=>add(t,owner.x,y+i*42,owner.w,40,{id:owner.id+'='+t,kind:'option',key:owner.id,local:true,option:t,overlay:true,active:t===owner.value}));}}
  return {size,entries,groups,notes,art,...extra};
 }
}
