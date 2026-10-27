import fs from 'node:fs';
import {flowLayout,titles,rangeValue} from './public/menus/flows.mjs';
const root=new URL('./public/menus/',import.meta.url),m=JSON.parse(fs.readFileSync(new URL('manifest.json',root))),content=JSON.parse(fs.readFileSync(new URL('menu-content.json',root))),a=m.assets.font_menu;
const width=t=>[...String(t)].reduce((n,c)=>n+(a.advance[c.charCodeAt(0)]??0),0)*a.scale;
let layouts=0,controls=0;const failures=[];
function check(page,s={},drop=null,data=content){
 const l=flowLayout(page,s,data,drop);layouts++;
 for(const e of l.entries){controls++;
  if(!['range','field','row','seat','room','check'].includes(e.kind)&&!m.assets['button-'+e.w+'-'+e.h+'-normal'])failures.push(page+': missing fitted button '+e.w+'x'+e.h);
  if(e.x<13||e.y<28||e.x+e.w>l.size[0]-13||e.y+e.h>l.size[1]-32)failures.push(page+': '+e.id+' outside frame well');
  const exempt=['range','field','row','seat','room','colour'].includes(e.kind),label=e.kind==='drop'?e.value:e.value??e.label;
  if(!exempt&&width(label)>e.w-(e.kind==='check'?48:e.kind==='drop'?44:20))failures.push(page+': '+e.id+' text overflow');
  if(e.kind==='drop'&&!e.options.includes(e.value))failures.push(page+': '+e.id+' selected value absent from choices');
  if(e.labelX!==undefined&&width(e.label)>e.w)failures.push(page+': '+e.id+' label overflow');
  if(e.kind==='range'&&(width(rangeValue(e))>e.w/2||e.value<e.min||e.value>e.max))failures.push(page+': '+e.id+' range label/value');
  if(!e.overlay)for(const o of l.entries)if(o!==e&&!o.overlay&&e.x<o.x+o.w&&e.x+e.w>o.x&&e.y<o.y+o.h&&e.y+e.h>o.y)failures.push(page+': '+e.id+' overlaps '+o.id);
  if(page!=='confirm'&&e.y>=150&&e.y+e.h<=518&&!e.overlay){const g=l.groups.find(g=>!g.overlay&&e.x>=g.x+12&&e.x+e.w<=g.x+g.w-12&&e.y>=g.y+44&&e.y+e.h<=g.y+g.h-12);if(!g)failures.push(page+': '+e.id+' outside inset group');}
 }
 for(const n of l.notes){if(!n.wrap&&(n.center?n.x-width(n.text)/2<38||n.x+width(n.text)/2>l.size[0]-38:n.x+width(n.text)>l.size[0]-38))failures.push(page+': note overflows '+n.text);}
 for(const art of l.art){if(!m.assets[art.id])failures.push(page+': missing art '+art.id);if(art.x<13||art.y<28||art.x+art.w>l.size[0]-13||art.y+art.h>l.size[1]-32)failures.push(page+': art outside frame');}
 if(page==='lobby'){const chat=l.notes.find(n=>n.maxLines===11),input=l.entries.find(e=>e.id==='Message');if(!chat||chat.y+(chat.maxLines-1)*chat.lineHeight+a.cellH*a.scale>input.y-8)failures.push('lobby: chat transcript collides with input');for(const [w,h] of [[1280,720],[1920,1080],[2560,1440]]){const scale=Math.max(1,Math.floor(h/693));if(l.size[0]*scale>w-40||l.size[1]*scale>h-40)failures.push('lobby: expanded housing cannot fit '+w+'x'+h);}}
 return l;
}
for(const page of Object.keys(titles)){
 const l=check(page);
 for(const e of l.entries.filter(e=>e.kind==='drop'&&!e.disabled))check(page,{},e.id);
}
for(const group of ['Covert Ops','Special Ops'])check('special',{'special:filter':group});
for(const map of content.maps){check('skirmish',{mapId:map.id});check('lobby',{mapId:map.id});check('maps',{'maps:filter':map.group,'maps:selected':map.id});check('seat',{mapId:map.id},'Start position');}
for(const mission of content.usermaps)check('usermaps',{'usermaps:selected':mission.id});
for(const state of [{},{'Private Game':true},{'Internet Game':true,'Private Game':true}])check('multiplayer',state);
for(const page of ['special','usermaps','maps','skirmish'])check(page,{},null,{...content,maps:[],special:[],usermaps:[]});
for(const message of ['Exit OpenCNC3D?','Abort this mission and return to the main menu?','Delete the selected saved mission?'])check('confirm',{confirmMessage:message});
for(const id of ['Game speed','Scroll rate','Music volume','Sound volume','Speech volume'])for(const v of [0,id.includes('volume')?239:6])check('controls',{[id]:v});
const output={status:failures.length?'FAIL':'PASS',layouts,controls,checks:['all screens / source tabs / empty lists','text and frame-safe control geometry','list paging and source map seating bounds','range endpoints and native input ranges','all dropdown overlays / shared assets'],failures};
fs.writeFileSync('tmp/cnc-menu-redesign-2026-10-11/flow-preflight.json',JSON.stringify(output,null,2)+'\n');console.log(output);if(failures.length)process.exit(1);

for(const tab of ['Covert Ops','Special Ops']){const l=flowLayout('special',{'special:filter':tab},content);const filters=l.entries.filter(e=>e.kind==='filter');if(filters.length!==2)throw Error('Special Ops must have two category buttons');const rows=l.entries.filter(e=>e.kind==='row');if(rows.some(e=>!['GDI','Nod'].includes(e.badge)))throw Error('Missing faction badge');if(rows.some((e,i)=>i&&rows[i-1].badge==='Nod'&&e.badge==='GDI'))throw Error('Faction order');if(rows.some(e=>e.option==='TESTMAP'))throw Error('Test Maps must not be selectable');}
