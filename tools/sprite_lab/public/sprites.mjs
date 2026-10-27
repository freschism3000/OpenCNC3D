const $=id=>document.getElementById(id);
let catalog,unit,oldImage,newImage,last=0,active=true;
const state={action:'walk',facing:4,phase:0,playing:true,speed:1,zoom:6};
const names={stand_ready:'Standing ready',stand_guard:'Standing guard',walk:'Run / walk',fire_weapon:'Fire standing',prone:'Prone',idle1:'Idle 1',idle2:'Idle 2',fire_prone:'Fire prone'};
const title=a=>names[a.id]||a.name;
const action=()=>unit.actions.find(a=>a.id===state.action);
const replacement=()=>unit.after.actions[state.action];
function image(src){return new Promise((resolve,reject)=>{const im=new Image();im.onload=()=>resolve(im);im.onerror=()=>reject(Error('Could not load sprite atlas: '+src));im.src=src;});}
function setupUnit(){
 unit=catalog.units.find(u=>u.id===$('unit').value);
 $('animation').replaceChildren();
 const paired=document.createElement('optgroup');paired.label='Before / after';
 const original=document.createElement('optgroup');original.label='Original actions · replacement pending';
 for(const a of unit.actions){const option=new Option(title(a),a.id);(unit.after.actions[a.id]?paired:original).append(option);}
 $('animation').append(paired,original);$('animation').value=state.action;
 $('facing').replaceChildren(...unit.directions.map((d,i)=>new Option(d,String(i))));$('facing').value=String(state.facing);
 configure();
}
function configure(){
 const a=action(),b=replacement();state.phase=0;$('frame').max=a.count-1;$('frame').value=0;
 const boxing=!!a.boxing_camera_offset;
 $('facing').replaceChildren(...unit.directions.flatMap((d,i)=>boxing&&i!==2&&i!==6?[]:[new Option(d,String(i))]));
 if(boxing&&state.facing!==2&&state.facing!==6)state.facing=2;
 $('facing').value=String(state.facing);$('facing').disabled=!a.jump&&!boxing;
 const views=a.jump?' · 8 facings':boxing?' · 2 boxing views':'';
 $('before-count').textContent=`${a.count} frame${a.count===1?'':'s'}${views}`;
 $('after-count').textContent=b?`${b.count} frame${b.count===1?'':'s'}${views}`:'Not yet authored';
 $('before-note').textContent=`Original E1.SHP · ${title(a)} · authentic DOS palette.`;
 $('after-note').textContent=b?b.note:'No replacement is substituted for this original action.';
 $('missing').hidden=!!b;
 $('frame').disabled=a.count===1;draw();
}
function drawSprite(canvas,im,spec,index,multiplier){
 const rect=canvas.getBoundingClientRect(),dpr=window.devicePixelRatio||1;
 const w=Math.round(rect.width*dpr),h=Math.round(rect.height*dpr);
 if(canvas.width!==w||canvas.height!==h){canvas.width=w;canvas.height=h;}
 const ctx=canvas.getContext('2d');ctx.setTransform(dpr,0,0,dpr,0,0);ctx.imageSmoothingEnabled=false;
 ctx.fillStyle='#191c21';ctx.fillRect(0,0,rect.width,rect.height);
 const scale=state.zoom*multiplier,originX=Math.round(rect.width/2),originY=Math.round(rect.height*.70);
 ctx.strokeStyle='#30353d';ctx.lineWidth=1;ctx.beginPath();ctx.moveTo(16,originY+.5);ctx.lineTo(rect.width-16,originY+.5);ctx.stroke();
 if(!im||index==null)return;
 ctx.drawImage(im,(index%spec.columns)*spec.width,Math.floor(index/spec.columns)*spec.height,spec.width,spec.height,originX-spec.anchor[0]*scale,originY-spec.anchor[1]*scale,spec.width*scale,spec.height*scale);
}
function indices(){
 const a=action(),b=replacement(),oldStage=Math.min(a.count-1,Math.floor(state.phase*a.count));
 const newStage=b?Math.min(b.count-1,Math.floor(state.phase*b.count)):null;
 const opposite=!!a.boxing_camera_offset&&state.facing===6,offset=opposite?a.boxing_camera_offset:0;
 const mapped=b?.[opposite?'opposite_stage_frames':'stage_frames'];
 return {oldStage,newStage,oldIndex:a.start+(a.jump?state.facing*a.jump:offset)+oldStage,newIndex:b?(mapped?mapped[newStage]:b.start+(b.jump?state.facing*b.jump:offset)+newStage):null};
}
function draw(){
 if(!unit||!oldImage||!newImage)return;
 const a=action(),b=replacement(),i=indices();
 drawSprite($('before'),oldImage,unit.before,i.oldIndex,2);
 drawSprite($('after'),newImage,unit.after,i.newIndex,1);
 $('before').dataset.frameIndex=String(i.oldIndex);$('after').dataset.frameIndex=String(i.newIndex);
 $('caption').dataset.sourceFps=String(a.fps);
 $('frame').value=i.oldStage;$('frame-readout').value=`${i.oldStage+1} / ${a.count}`;
 const facing=a.jump||a.boxing_camera_offset?unit.directions[state.facing]:'fixed camera';
 $('caption').textContent=`${unit.name} · ${title(a)} · ${facing} · original ${i.oldStage+1}/${a.count}${b?` · redesign ${i.newStage+1}/${b.count}`:' · replacement pending'}`;
 $('before').setAttribute('aria-label',`Original ${unit.name}, ${title(a)}, ${facing}, frame ${i.oldStage+1}`);
 $('after').setAttribute('aria-label',b?`Redesigned ${unit.name}, ${title(a)}, ${facing}, frame ${i.newStage+1}`:'Replacement animation not yet authored');
}
function play(value){state.playing=value;$('play').textContent=value?'Pause':'Play';$('play').setAttribute('aria-pressed',String(value));}
function tick(now){
 const dt=last?Math.min(.1,(now-last)/1000):0;last=now;
 if(unit&&active&&!document.hidden&&state.playing&&action().count>1){state.phase=(state.phase+dt*action().fps/action().count*state.speed)%1;draw();}
 requestAnimationFrame(tick);
}
for(const [id,key] of [['speed','speed'],['zoom','zoom'],['facing','facing']])$(id).addEventListener('change',()=>{state[key]=Number($(id).value);draw();});
$('animation').addEventListener('change',()=>{state.action=$('animation').value;configure();});
$('play').addEventListener('click',()=>play(!state.playing));
$('reset').addEventListener('click',()=>{state.phase=0;draw();});
$('frame').addEventListener('input',()=>{play(false);state.phase=Number($('frame').value)/action().count;draw();});
$('unit').addEventListener('change',async()=>{setupUnit();[oldImage,newImage]=await Promise.all([image(unit.before.image),image(unit.after.image)]);draw();});
new ResizeObserver(draw).observe(document.querySelector('main'));
new IntersectionObserver(([entry])=>{active=entry.isIntersecting;last=0;}).observe(document.querySelector('main'));
async function init(){
 const response=await fetch('data/catalog.json',{cache:'no-store'});if(!response.ok)throw Error('Could not load sprite collection');catalog=await response.json();
 $('unit').replaceChildren(...catalog.units.map(u=>new Option(u.name,u.id)));setupUnit();
 [oldImage,newImage]=await Promise.all([image(unit.before.image),image(unit.after.image)]);draw();
 document.body.dataset.ready='true';requestAnimationFrame(tick);
}
init().catch(error=>{$('caption').textContent=error.message;document.body.dataset.error=error.message;for(const e of document.querySelectorAll('button,select,input'))e.disabled=true;});
// Compact browser QA access: the displayed indices always derive from one phase.
window.spriteLab={state,get catalog(){return catalog;},indices,draw};
