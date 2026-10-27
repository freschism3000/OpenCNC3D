// Actual Visuals labels/choices from current main game/dosopt.c; local review values.
export const categories=['Display','World','Effects','Interface'];
export const defaults={'Graphics mode':'Enhanced','Display mode':'Borderless','Resolution':'Desktop','Vertical sync':'ON','FPS counter':'OFF','UI scaling':'1x','Perspective':'Isometric','Smooth animations':'ON','New HUD':'ON','Bilinear filtering':'ON','Water Shader':'ON','3D Trees':'ON','3D Tiberium':'ON','Terrain':'Enhanced Textures','Infantry':'DOS Sprites','Console output gamma':'ON','Sun shadows':'ON','Ambient occlusion':'ON','Dynamic light':'ON','Bloom':'ON','Colour grade':'ON','CRT':'OFF'};
const globalKeys=new Set(['Graphics mode','Display mode','Resolution','Vertical sync','FPS counter']);
export const profile=s=>s['Graphics mode']??defaults['Graphics mode'];
export const key=(id,s)=>globalKeys.has(id)?id:profile(s)+':'+id;
export const value=(id,s)=>s[key(id,s)]??(profile(s)==='Classic'&&!globalKeys.has(id)?'OFF':id==='Terrain'&&profile(s)==='N64'?'N64 Textures':defaults[id]);
const n64=new Set(['UI scaling','Smooth animations','New HUD','Bilinear filtering','Terrain','Infantry']);
export function visualLayout(section,s,openDrop){
 const entries=[],groups=[],notes=[];
 const add=(label,x,y,w,h,extra={})=>entries.push({id:label,label,x,y,w,h,...extra});
 const available=id=>id==='Resolution'?value('Display mode',s)!=='Borderless':id==='UI scaling'?profile(s)!=='Classic'&&value('New HUD',s)==='ON':globalKeys.has(id)||profile(s)==='Enhanced'||id==='Smooth animations'||profile(s)==='N64'&&n64.has(id);
 const group=(title,x,y,w,h)=>groups.push({title,x,y,w,h});
 const check=(id,x,y)=>add(id,x,y,424,36,{kind:'check',setting:true,value:value(id,s),disabled:!available(id)});
 const drop=(id,x,y,options)=>add(id,x,y,424,40,{kind:'drop',setting:true,value:id==='Resolution'&&value('Display mode',s)==='Borderless'?'Desktop':value(id,s),labelX:x,labelY:y-26,options,disabled:!available(id)});
 const radios=(id,labels,x,y)=>labels.forEach((label,i)=>add(label,x+i*144,y,140,40,{id:id+':'+label,kind:'radio',setting:true,key:id,option:label,active:value(id,s)===label}));
 categories.forEach((label,i)=>add(label,38+i*238,86,230,40,{tab:true,active:label.toLowerCase()===section}));
 if(section==='display'){
  group('DISPLAY',38,160,460,358);group('GRAPHICS MODE',518,160,460,180);group('OUTPUT',518,360,460,158);
  radios('Display mode',['Fullscreen','Windowed','Borderless'],54,208);
  drop('Resolution',56,294,['Desktop','1280 X 720','1920 X 1080','2560 X 1440']);
  check('Vertical sync',56,356);check('FPS counter',56,408);
  notes.push({text:'Desktop: 2560 X 1440',x:56,y:476});
  radios('Graphics mode',['Classic','N64','Enhanced'],534,208);
  notes.push({text:profile(s)==='Enhanced'?'Enhanced 3D rendering':profile(s)==='Classic'?'Original DOS presentation':'Nintendo 64 presentation',x:536,y:278});
  check('Console output gamma',536,406);
 }else if(section==='world'){
  group('SURFACES',38,160,460,358);group('OBJECTS',518,160,460,358);
  drop('Terrain',56,244,['N64 Textures','DOS Textures','Enhanced Textures']);
  check('Water Shader',56,314);check('Bilinear filtering',56,366);
  drop('Infantry',536,244,['N64 Sprites','DOS Sprites']);
  check('3D Trees',536,314);check('3D Tiberium',536,366);
 }else if(section==='effects'){
  group('LIGHTING',38,160,460,358);group('POST PROCESSING',518,160,460,358);
  ['Sun shadows','Ambient occlusion','Dynamic light'].forEach((id,i)=>check(id,56,210+i*52));
  ['Bloom','Colour grade','CRT'].forEach((id,i)=>check(id,536,210+i*52));
 }else{
  group('INTERFACE',38,160,460,358);group('CAMERA',518,160,460,358);
  check('New HUD',56,210);check('Smooth animations',56,262);
  drop('UI scaling',56,362,['1x','-2x']);
  drop('Perspective',536,244,['Classic','Isometric']);
  notes.push({text:'Classic: top-down view',x:536,y:326},{text:'Isometric: angled view',x:536,y:358});
 }
 add('Reset to Defaults',38,550,292,40);add('Back',550,550,192,40);add('OK',758,550,220,40,{primary:true});
 if(openDrop){
  const owner=entries.find(e=>e.id===openDrop&&!e.disabled);
  if(owner){const height=owner.options.length*42+8;let y=owner.y+owner.h+4;if(y+height>530)y=owner.y-height-4;
   groups.push({title:null,x:owner.x-4,y:y-4,w:owner.w+8,h:height,overlay:true});
   owner.options.forEach((label,i)=>add(label,owner.x,y+i*42,owner.w,40,{id:owner.id+'='+label,kind:'option',key:owner.id,option:label,active:owner.value===label,overlay:true}));
  }
 }
 return {size:[1016,636],entries,groups,notes};
}
