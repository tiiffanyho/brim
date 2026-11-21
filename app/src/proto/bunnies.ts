// Brim and friends, drawn in code (same generator as the Figma file): soft white bunnies with
// wobbly edges on purpose. Deterministic: same seed, same bunny.
/* eslint-disable */
// @ts-nocheck -- generated drawing maths; output is a plain SVG string
function build(): Record<string, string> {
const WHITE='#FFFFFF',PINK='#F6B8C4',BLUSH='#F9C9D2',EYE='#2A2140',SHADOW='#E4DAF7',PURPLE='#7C3AED',PURPLE_D='#5B21B6',LENS='#1B1530';
const rng=s=>()=>{s|=0;s=s+0x6D2B79F5|0;let t=Math.imul(s^s>>>15,1|s);t=t+Math.imul(t^t>>>7,61|t)^t;return((t^t>>>14)>>>0)/4294967296};
const R=Math.round,rad=d=>d*Math.PI/180;
function wob(cx,cy,rx,ry,rot,amt,seed){const r=rng(seed*7919+13),ph=[r()*6.28,r()*6.28,r()*6.28],n=12,p=[],c=Math.cos(rad(rot)),s=Math.sin(rad(rot));
for(let i=0;i<n;i++){const a=2*Math.PI*i/n,k=1+amt*(.6*Math.sin(2*a+ph[0])+.3*Math.sin(3*a+ph[1])+.25*Math.sin(5*a+ph[2]))+(r()-.5)*amt*.5,x=rx*k*Math.cos(a),y=ry*k*Math.sin(a);p.push([cx+x*c-y*s,cy+x*s+y*c]);}
let d=`M${R(p[0][0])} ${R(p[0][1])}`;for(let i=0;i<n;i++){const p0=p[(i-1+n)%n],p1=p[i],p2=p[(i+1)%n],p3=p[(i+2)%n];
d+=`C${R(p1[0]+(p2[0]-p0[0])/6)} ${R(p1[1]+(p2[1]-p0[1])/6)} ${R(p2[0]-(p3[0]-p1[0])/6)} ${R(p2[1]-(p3[1]-p1[1])/6)} ${R(p2[0])} ${R(p2[1])}`;}return d+'Z';}
const blob=(cx,cy,rx,ry,rot=0,fill=WHITE,amt=.05,seed=0,op=1)=>`<path d="${wob(cx,cy,rx,ry,rot,amt,seed)}" fill="${fill}"${op<1?` opacity="${op}"`:''}/>`;
function ear(cx,cy,L,w,rot,seed,inner=true){let s=blob(cx,cy,w,L,rot,WHITE,.07,seed);if(inner){const a=rad(rot),ox=-Math.sin(a)*L*.06+Math.cos(a)*w*.1,oy=Math.cos(a)*L*.06+Math.sin(a)*w*.1;s+=blob(cx+ox,cy+oy,w*.45,L*.66,rot+3,PINK,.1,seed+7);}return s;}
const eye=(x,y,seed=0,closed=false)=>{if(closed)return `<path d="M${x-5} ${y}q5 4 10 .6" stroke="${EYE}" stroke-width="2.2" fill="none" stroke-linecap="round"/>`;const r=rng(seed);return `<ellipse cx="${x}" cy="${y}" rx="${(3.4*(.9+r()*.2)).toFixed(1)}" ry="${(3.4*(1.05+r()*.2)).toFixed(1)}" fill="${EYE}"/>`;};
const blush=(x,y,seed)=>blob(x,y,7.5,4.6,(rng(seed)()-.5)*20,BLUSH,.12,seed,.9);
const shadow=(cx,cy,rx,seed)=>blob(cx,cy,rx,rx*.16,0,SHADOW,.08,seed,.9);
const B={};
B.sit=shadow(112,196,58,1)+ear(92,58,42,13,-12,11)+ear(118,54,44,13,10,12)+blob(110,150,54,48,0,WHITE,.05,2)+blob(118,104,38,33,8,WHITE,.05,3)+blob(78,178,16,9,-5,WHITE,.08,4)+blob(60,160,13,13,0,WHITE,.1,5)+eye(136,100,6)+blush(140,115,7);
B.hop=shadow(112,190,70,8)+ear(134,76,38,11,-62,13)+ear(142,66,40,11,-48,14)+blob(105,135,66,30,-12,WHITE,.06,15)+blob(160,98,31,27,10,WHITE,.05,16)+blob(48,158,22,9,25,WHITE,.1,17)+blob(150,150,16,7,-30,WHITE,.1,18)+blob(40,128,11,11,0,WHITE,.1,19)+eye(172,92,20)+blush(176,106,21);
B.back=shadow(110,196,52,22)+ear(92,52,44,13,-6,23,false)+ear(126,50,46,13,8,24,false)+blob(110,148,50,50,0,WHITE,.05,25)+blob(110,98,36,32,0,WHITE,.05,26)+blob(108,172,13,12,0,'#F7F3FF',.12,27);
B.sleep=shadow(110,186,66,28)+blob(110,150,66,36,0,WHITE,.05,29)+blob(150,140,30,26,0,WHITE,.05,30)+ear(112,124,34,11,76,31)+ear(116,132,32,10,88,32)+eye(160,138,0,true)+blush(166,150,33)+blob(48,152,11,11,0,WHITE,.1,34);
B.curious=shadow(108,196,50,35)+ear(96,54,42,12,-18,36)+ear(132,84,34,12,52,37)+blob(106,150,48,46,0,WHITE,.05,38)+blob(108,106,36,32,-8,WHITE,.05,39)+eye(118,102,40)+eye(96,104,41)+blush(124,116,42)+blush(88,118,43);
B.brim=shadow(112,196,58,44)+ear(96,46,40,12,-14,45)+ear(126,44,42,12,12,46)+blob(110,152,52,46,0,WHITE,.05,47)+blob(114,108,38,33,4,WHITE,.05,48)+blob(114,86,33,15,6,PURPLE,.06,49)+blob(148,94,22,5.5,10,PURPLE_D,.08,50)+`<circle cx="159" cy="96" r="4.2" fill="${LENS}"/><circle cx="160.1" cy="94.9" r="1.3" fill="#C4B5FD"/>`+blob(78,178,16,9,-5,WHITE,.08,51)+blob(60,162,13,13,0,WHITE,.1,52)+eye(134,110,53)+blush(140,123,54);
B.wave=shadow(110,198,44,55)+ear(98,40,40,12,-8,56)+ear(124,40,41,12,10,57)+blob(110,150,40,52,0,WHITE,.05,58)+blob(111,90,35,31,0,WHITE,.05,59)+blob(152,118,9,17,35,WHITE,.1,60)+blob(88,196,14,7,0,WHITE,.1,61)+blob(132,196,14,7,0,WHITE,.1,62)+eye(122,88,63)+eye(100,89,64)+blush(128,101,65)+blush(93,102,66);
const out: Record<string, string> = {};for(const k in B)out[k]=`<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 220 220" aria-hidden="true" focusable="false">${B[k]}</svg>`;return out;}
export type BunnyName = "brim" | "sit" | "hop" | "back" | "sleep" | "curious" | "wave";
let cache: Record<string, string> | null = null;
export const bunnySvg = (name: BunnyName): string => (cache ??= build())[name] ?? "";
