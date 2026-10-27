// Website FBX geometry, corner normals, UVs and embedded textures, fitted to a UI seat.
let renderers={},titleRenderers={};
const vertex=`attribute vec3 position; attribute vec3 normal; attribute vec2 uv;
uniform float angle; varying vec2 texUV; varying float light;
vec3 turn(vec3 p){float c=cos(angle),s=sin(angle),a=cos(.34),b=sin(.34);float u=c*p.x+s*p.z,w=-s*p.x+c*p.z;return vec3(u,a*p.y-b*w,b*p.y+a*w);}
void main(){vec3 p=turn(position),n=turn(normal);gl_Position=vec4(p.xy*.86,-p.z*.25,1.);texUV=vec2(uv.x,1.-uv.y);light=min(1.,.36+.7*max(0.,dot(n,vec3(.25,.5,.83)))+.42*pow(max(0.,dot(n,vec3(.13,.25,.96))),30.));}`;
const fragment=`precision mediump float;uniform sampler2D image;uniform float orange;varying vec2 texUV;varying float light;
void main(){vec4 q=texture2D(image,texUV);if(orange>.5 && q.r>q.g*1.1 && q.r>q.b*1.4){q.g*=.75;q.b*=.45;}gl_FragColor=vec4(q.rgb*light,q.a);}`;
async function renderer(model,orange,size=28){
 const canvas=document.createElement('canvas');canvas.width=canvas.height=size;
 const gl=canvas.getContext('webgl',{alpha:true,antialias:false,preserveDrawingBuffer:true});if(!gl)throw new Error('3D faction badges require WebGL');
 const compile=(type,text)=>{const s=gl.createShader(type);gl.shaderSource(s,text);gl.compileShader(s);if(!gl.getShaderParameter(s,gl.COMPILE_STATUS))throw new Error(gl.getShaderInfoLog(s));return s;};
 const program=gl.createProgram();gl.attachShader(program,compile(gl.VERTEX_SHADER,vertex));gl.attachShader(program,compile(gl.FRAGMENT_SHADER,fragment));gl.linkProgram(program);if(!gl.getProgramParameter(program,gl.LINK_STATUS))throw new Error(gl.getProgramInfoLog(program));gl.useProgram(program);
 const geometry=gl.createBuffer();gl.bindBuffer(gl.ARRAY_BUFFER,geometry);gl.bufferData(gl.ARRAY_BUFFER,new Float32Array(model.vertices),gl.STATIC_DRAW);
 for(const [name,count,offset] of [['position',3,0],['normal',3,12],['uv',2,24]]){const loc=gl.getAttribLocation(program,name);gl.enableVertexAttribArray(loc);gl.vertexAttribPointer(loc,count,gl.FLOAT,false,32,offset);}
 const groups=[];
 for(let m=0;m<model.textures.length;m++){
  const image=new Image();image.src=model.textures[m];await image.decode();
  const texture=gl.createTexture();gl.bindTexture(gl.TEXTURE_2D,texture);gl.texImage2D(gl.TEXTURE_2D,0,gl.RGBA,gl.RGBA,gl.UNSIGNED_BYTE,image);gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_MIN_FILTER,gl.NEAREST);gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_MAG_FILTER,gl.NEAREST);gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_WRAP_S,gl.CLAMP_TO_EDGE);gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_WRAP_T,gl.CLAMP_TO_EDGE);
  const indices=[];model.materials.forEach((v,i)=>{if(v===m)indices.push(i*3,i*3+1,i*3+2);});const buffer=gl.createBuffer();gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER,buffer);gl.bufferData(gl.ELEMENT_ARRAY_BUFFER,new Uint16Array(indices),gl.STATIC_DRAW);groups.push({texture,buffer,count:indices.length});
 }
 const angleLoc=gl.getUniformLocation(program,'angle');gl.uniform1f(gl.getUniformLocation(program,'orange'),orange);gl.uniform1i(gl.getUniformLocation(program,'image'),0);gl.enable(gl.DEPTH_TEST);gl.depthFunc(gl.LEQUAL);let last=-1;
 return angle=>{const phase=Math.floor(angle*100)/100;if(phase!==last){last=phase;gl.clearColor(0,0,0,0);gl.clear(gl.COLOR_BUFFER_BIT|gl.DEPTH_BUFFER_BIT);gl.uniform1f(angleLoc,phase);for(const g of groups){gl.bindTexture(gl.TEXTURE_2D,g.texture);gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER,g.buffer);gl.drawElements(gl.TRIANGLES,g.count,gl.UNSIGNED_SHORT,0);}}return canvas;};
}
export async function loadFactionMeshes(){const meshes=await(await fetch('faction-meshes.json')).json();renderers.gdi=await renderer(meshes.gdi,1);renderers.nod=await renderer(meshes.nod,0);titleRenderers.gdi=await renderer(meshes.gdi,1,200);titleRenderers.nod=await renderer(meshes.nod,0,200);}
export function drawFactionBadge(ctx,faction,x,y,size,angle){const draw=(size>28?titleRenderers:renderers)[faction==='Nod'?'nod':'gdi'];if(draw)ctx.drawImage(draw(angle),x,y,size,size);}
