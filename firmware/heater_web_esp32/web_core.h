// Generated from web_src/core.js by build_pages.py - edit the source, not this file.
#pragma once
#include <pgmspace.h>
const char CORE_JS[] PROGMEM = R"PG(/* Shed Heater page core v1.4 - shared by every page style (burner / lcd / dash).
Each style page defines draw(j,d) and has #hdr, #alert and #common placeholders. */
(function(){
var W=window,doc=document;
function $(i){return doc.getElementById(i)}
W.$=$;
var H=W.H={j:null,d:null,unit:'C',unitHold:0,hist:null,gUnit:''};
var THEMES=[['burner','Burner'],['lcd','LCD'],['dash','Dash']];
var PH={IDLE:['OFF','Standby'],START:['STARTING','Start requested'],FAN:['FAN UP','Fan spinning up'],
PREHEAT:['PREHEAT','Glow plug heating'],IGNITION:['IGNITION','Fuel on, lighting'],RUN:['RUN','Burning'],
SHUTDOWN:['SHUTDOWN','Fuel off, burning out'],CLEARING:['CLEARING','Glow plug clearing the chamber'],
PURGE:['PURGE','Fan cooling the chamber'],COOL:['COOLING','Cooling down'],FAULT:['FAULT','Stopped on a fault'],
UNK:['WAITING','Waiting for the heater']};
W.PH=PH;
var css=':root{--bg:#0d1117;--panel:#161c24;--line:#2a323d;--fg:#e8edf2;--fg2:#c5cdd6;--dim:#9aa3ad;--acc:#ff8c32;--accFg:#1a0b00;'+
'--warn:#ffb000;--warnBg:#2e2306;--warnFg:#ffe0a0;--bad:#ff5a4d;--badBg:#3a1210;--badFg:#ffc4be;--logBg:#0a0e13;--logFg:#a9b4c0;'+
'--gLine:#ffb347;--gRun:#ff8c32;--gCool:#4aa8e8;--grid:#232a33;color-scheme:dark}'+
'body{margin:0;background:var(--bg);color:var(--fg);font-family:system-ui,-apple-system,"Segoe UI",Roboto,sans-serif}'+
'.wrap{max-width:480px;margin:0 auto;padding:16px;box-sizing:border-box;display:flex;flex-direction:column;gap:12px}'+
'.c-top{display:flex;justify-content:space-between;align-items:center;gap:8px}'+
'.c-top h1{margin:0;font-size:20px;font-weight:700;white-space:nowrap}'+
'.c-sub{font-size:12px;color:var(--dim);margin-top:2px}'+
'.c-ctl{display:flex;gap:6px;align-items:center}'+
'.c-sel{-webkit-appearance:none;appearance:none;background:var(--panel) url("data:image/svg+xml,%3Csvg xmlns=%27http://www.w3.org/2000/svg%27 viewBox=%270 0 10 6%27%3E%3Cpath d=%27M1 1l4 4 4-4%27 fill=%27none%27 stroke=%27%239aa3ad%27 stroke-width=%271.6%27/%3E%3C/svg%3E") no-repeat right 9px center/10px 6px;'+
'color:var(--fg);border:1px solid var(--line);border-radius:10px;padding:8px 26px 8px 10px;font:700 14px system-ui,-apple-system,"Segoe UI",Roboto,sans-serif;cursor:pointer}'+
'.c-seg{display:flex;background:var(--panel);border:1px solid var(--line);border-radius:10px;overflow:hidden}'+
'.c-seg button{padding:8px 11px;border:0;font-size:15px;font-weight:700;background:transparent;color:var(--dim);cursor:pointer}'+
'.c-seg button.sel{background:var(--acc);color:var(--accFg)}'+
'.c-alert{display:none;gap:10px;align-items:flex-start;border-radius:12px;padding:10px 12px;font-size:14px;font-weight:600;line-height:1.35;border:1px solid}'+
'.c-alert svg{flex:none;margin-top:1px}'+
'.c-alert.warn{display:flex;background:var(--warnBg);border-color:var(--warn);color:var(--warnFg)}'+
'.c-alert.bad{display:flex;background:var(--badBg);border-color:var(--bad);color:var(--badFg)}'+
'.c-common{display:flex;flex-direction:column;gap:12px}'+
'.c-stats{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:6px}'+
'.c-st{background:var(--panel);border:1px solid var(--line);border-radius:10px;padding:8px 10px;min-width:0}'+
'.c-st span{display:block;font-size:10px;font-weight:700;letter-spacing:.1em;color:var(--dim);text-transform:uppercase;white-space:nowrap}'+
'.c-st b{display:block;margin-top:3px;font-size:17px;font-weight:800;color:var(--fg);font-variant-numeric:tabular-nums;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}'+
'.c-st b.warn{color:var(--warn)}.c-st b.bad{color:var(--bad)}'+
'.c-card{background:var(--panel);border:1px solid var(--line);border-radius:12px;padding:10px 12px}'+
'.c-gh{display:flex;justify-content:space-between;align-items:baseline;font-size:12px;font-weight:700;letter-spacing:.06em;color:var(--dim);text-transform:uppercase}'+
'.c-gh b{color:var(--fg);font-weight:700;letter-spacing:0;text-transform:none}'+
'#cGraph{width:100%;height:auto;display:block;margin-top:6px;touch-action:pan-y}'+
'.g-grid{stroke:var(--grid);stroke-width:1}.g-ax{font:600 10px system-ui,-apple-system,"Segoe UI",Roboto,sans-serif;fill:var(--dim)}'+
'.g-line{fill:none;stroke:var(--gLine);stroke-width:2;stroke-linejoin:round;stroke-linecap:round}.g-area{fill:var(--gLine);opacity:.12}'+
'.g-x{stroke:var(--dim);stroke-width:1;stroke-dasharray:3 3}.g-dot{fill:var(--gLine);stroke:var(--panel);stroke-width:2}'+
'.g-tip{font:700 11px system-ui,-apple-system,"Segoe UI",Roboto,sans-serif;fill:var(--fg)}.g-empty{font:600 12px system-ui,-apple-system,"Segoe UI",Roboto,sans-serif;fill:var(--dim)}'+
'.c-leg{display:flex;gap:12px;align-items:center;margin-top:6px;font-size:11px;color:var(--dim)}'+
'.c-leg i{display:inline-block;width:12px;height:5px;border-radius:2px;margin-right:4px;vertical-align:middle}'+
'.c-btn{height:40px;border-radius:10px;border:1px solid var(--line);background:var(--panel);color:var(--fg2);font-size:13px;cursor:pointer;width:100%}'+
'.c-log{background:var(--logBg);border:1px solid var(--line);border-radius:12px;padding:10px 12px;font-family:ui-monospace,"SF Mono",Menlo,Consolas,monospace;font-size:12px;line-height:1.45;color:var(--logFg);white-space:pre-wrap;max-height:180px;overflow-y:auto}'+
'@media (prefers-reduced-motion:reduce){*{animation:none!important;transition:none!important}}';
var st=doc.createElement('style');st.textContent=css;doc.head.insertBefore(st,doc.head.firstChild);
function build(){
var h=$('hdr'),opts='';
THEMES.forEach(function(t){opts+='<option value="'+t[0]+'"'+(t[0]==W.THEME?' selected':'')+'>'+t[1]+'</option>'});
if(h)h.innerHTML='<div class="c-top"><h1>Shed Heater</h1><div class="c-ctl">'+
'<select id="cTheme" class="c-sel" aria-label="Page style">'+opts+'</select>'+
'<div class="c-seg"><button id="uC" aria-label="Show Celsius">&deg;C</button><button id="uF" aria-label="Show Fahrenheit">&deg;F</button></div>'+
'</div></div><div class="c-sub" id="cSub">connecting...</div>';
var a=$('alert');
if(a){a.className='c-alert';a.setAttribute('role','status');
a.innerHTML='<svg viewBox="0 0 24 24" width="20" height="20" aria-hidden="true"><path d="M12 3L22 20H2Z M12 9v5 M12 17.2v.1" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round"/></svg><span id="cAlertT"></span>';}
var c=$('common');
if(c){c.className='c-common';c.innerHTML=
'<div class="c-stats">'+
'<div class="c-st"><span>Idle volts</span><b id="sV">--</b></div>'+
'<div class="c-st"><span>This run</span><b id="sRun">--</b></div>'+
'<div class="c-st"><span>Light-up</span><b id="sLight">--</b></div>'+
'<div class="c-st"><span id="sTodayL">Today</span><b id="sToday">--</b></div>'+
'<div class="c-st"><span>Total</span><b id="sTot">--</b></div>'+
'<div class="c-st"><span>Starts</span><b id="sStarts">--</b></div></div>'+
'<div class="c-card"><div class="c-gh"><span>Chamber &middot; last hour</span><b id="gPeak"></b></div>'+
'<svg id="cGraph" viewBox="0 0 340 124" role="img" aria-label="Chamber temperature over the last hour"></svg>'+
'<div class="c-leg"><span><i style="background:var(--gRun)"></i>Running</span><span><i style="background:var(--gCool)"></i>Cooling</span><span><i style="background:var(--bad)"></i>Fault</span></div></div>'+
(W.NOPOLL?'':'<button class="c-btn" id="cPoll">Chamber poll: -</button>')+
'<div class="c-log" id="cLog"></div>';}
if($('uC')){$('uC').onclick=function(){setUnit('C')};$('uF').onclick=function(){setUnit('F')};}
if($('cTheme'))$('cTheme').onchange=function(){setTheme(this.value)};
if($('cPoll'))$('cPoll').onclick=function(){cyclePoll()};
var g=$('cGraph');
if(g){g.addEventListener('pointermove',hover);g.addEventListener('pointerdown',hover);g.addEventListener('pointerleave',function(){hover(null)});}
}
W.cmd=function(c,n){fetch('/api/cmd?c='+c+(n!==undefined?'&n='+n:'')).then(function(){setTimeout(tick,300)}).catch(function(){})};
W.setUnit=function(u){H.unit=u;H.unitHold=Date.now()+4000;render();fetch('/api/unit?u='+u).catch(function(){})};
W.cyclePoll=function(){var p=H.j?H.j.poll:-1;if(p<0)return;cmd('chamber',p==5?30:(p==30?0:5))};
W.mainBtn=function(){if(H.d&&H.d.action)cmd(H.d.action)};
W.setTheme=function(t){fetch('/api/theme?t='+t).then(function(){location.href='/'}).catch(function(){location.href='/'+t})};
W.T=function(c){return H.unit=='F'?c*9/5+32:c};
W.fmtT=function(c,ap){return (c===null||c===undefined)?'--':(ap?'~':'')+Math.round(T(c))};
W.rgb=function(c){return 'rgb('+c[0]+','+c[1]+','+c[2]+')'};
W.mix=function(stops,x){
if(x<=stops[0][0])return rgb(stops[0][1]);
for(var i=1;i<stops.length;i++){if(x<=stops[i][0]){var a=stops[i-1],b=stops[i],f=(x-a[0])/(b[0]-a[0]);
return rgb([0,1,2].map(function(k){return Math.round(a[1][k]+(b[1][k]-a[1][k])*f)}))}}
return rgb(stops[stops.length-1][1]);
};
function pad(n){return n<10?'0'+n:''+n}
function dur(s){if(s<60)return s+' s';if(s<600)return Math.floor(s/60)+'m '+pad(s%60)+'s';if(s<3600)return Math.floor(s/60)+' min';return Math.floor(s/3600)+'h '+pad(Math.floor(s%3600/60))+'m'}
function hrs(s){return s<36000?(s/3600).toFixed(1)+' h':Math.round(s/3600)+' h'}
W.dur=dur;
// Phase from the flags, for a v1.1 bridge that doesn't send one.
function guess(st,f,p,g){
if(st=='OFF')return 'IDLE';if(st=='FAULT')return 'FAULT';
if(st=='RUN'){if(!f&&!p&&!g)return 'START';if(f&&!p&&!g)return 'FAN';if(f&&!p&&g)return 'PREHEAT';if(p&&g)return 'IGNITION';return 'RUN'}
if(st=='COOL'){if(p)return 'SHUTDOWN';if(g)return 'CLEARING';if(f)return 'PURGE';return 'COOL'}
return 'UNK';
}
function derive(j){
var d={};
d.dead=j.stale?'NO LINK':(j.age>5?'NO REPLY':'');
var st=d.dead?'':j.state;d.st=st;
d.off=st=='OFF';d.run=st=='RUN';d.cool=st=='COOL';d.fault=st=='FAULT';
d.fan=!d.dead&&!!j.run;d.glow=!d.dead&&!!j.glow;d.pump=!d.dead&&!!j.pump;
var ph=d.dead?'UNK':(j.phase||guess(st,d.fan,d.pump,d.glow));d.phase=ph;
d.starting=d.run&&ph!='RUN';
d.burn=d.run&&d.pump;
d.level=d.run?j.level:0;
d.c=(d.dead||j.chamberC===null||j.chamberC===undefined)?null:j.chamberC;d.approx=!!j.approx;
d.ecode=(d.fault&&j.ecode>=0)?j.ecode:-1;
d.ecodeStr=d.ecode>=0?'E-'+pad(d.ecode):'';
var p=PH[ph]||PH.UNK;
d.label=d.dead?d.dead:ph=='RUN'?'RUN · L'+d.level:(d.fault&&d.ecodeStr)?'FAULT '+d.ecodeStr:p[0];
d.desc=d.dead?(j.stale?'No data from the Nano':'Heater not answering'):ph=='RUN'?'Burning at level '+d.level:
d.fault?(d.ecodeStr?'Fault '+d.ecodeStr+' · ':'')+(j.bridge>=12?'tap START to clear':'clear it on the heater display'):p[1];
d.canClear=d.fault&&j.bridge>=12;
d.action=d.dead?null:d.off?'on':d.run?'off':d.canClear?'on':null;
d.actionText=d.dead?d.dead:d.off?'START':d.run?'STOP':d.canClear?'CLEAR + START':d.fault?'FAULT':d.cool?'COOLING DOWN':'WAITING';
d.volts=j.volts>0?j.volts:null;
return d;
}
W.derive=derive;
function render(){
if($('uC')){$('uC').className=H.unit=='C'?'sel':'';$('uF').className=H.unit=='F'?'sel':'';}
if(H.gUnit!=H.unit)drawGraph();
var j=H.j;if(!j)return;
var d=H.d=derive(j);
if($('cSub'))$('cSub').textContent=(j.mode=='PB'?'piggyback':j.mode=='SA'?'standalone':'--')+' · '+j.ip+
(j.bridge&&j.bridge<12?' · Nano on bridge v1.1':'');
var a=$('alert');
if(a){if(j.alert){a.className='c-alert '+(j.alertLvl=='bad'?'bad':'warn');$('cAlertT').textContent=j.alert}else a.className='c-alert'}
var s=j.stats||{};
if($('sV')){
var v=$('sV');
if(d.volts){var lo=d.volts<=11,hi=d.volts>=15;v.textContent=d.volts+' V'+(lo?' low':hi?' high':'');v.className=d.volts<=10?'bad':(lo||hi)?'warn':''}
else{v.textContent='--';v.className=''}
$('sRun').textContent=s.runFor>=0?dur(s.runFor):'--';
$('sLight').textContent=s.light>=0?dur(s.light):'--';
$('sTodayL').textContent=s.clock?'Today':'Since boot';
$('sToday').textContent=s.today!==undefined?dur(s.today):'--';
$('sTot').textContent=s.total!==undefined?hrs(s.total):'--';
$('sStarts').textContent=s.starts!==undefined?s.starts:'--';
}
if($('cPoll'))$('cPoll').textContent=j.poll<0?'Chamber poll: needs bridge v1.1+':'Chamber poll: '+(j.poll>0?'every '+j.poll+'s':'off')+' (tap to change)';
if($('cLog'))$('cLog').textContent=j.log;
try{if(W.draw)W.draw(j,d)}catch(e){if(W.console)console.log(e)}
}
W.render=render;
// ---- chamber history graph: one series, fixed scale, state band underneath ----
var G={x0:34,x1:334,y0:8,y1:92};
function gy(t,max){return G.y1-(G.y1-G.y0)*Math.max(0,Math.min(max,t))/max}
function drawGraph(){
var g=$('cGraph');H.gUnit=H.unit;if(!g||!H.hist)return;
var h=H.hist,c=h.c||[],sb=h.s||'',n=c.length,F=H.unit=='F';
if(n<2)return;
var max=F?500:250,ticks=F?[0,200,400]:[0,100,200],o='',i;
function X(k){return G.x0+(G.x1-G.x0)*k/(n-1)}
ticks.forEach(function(t){var y=gy(t,max).toFixed(1);
o+='<line class="g-grid" x1="'+G.x0+'" x2="'+G.x1+'" y1="'+y+'" y2="'+y+'"/><text class="g-ax" x="'+(G.x0-6)+'" y="'+(+y+3.5)+'" text-anchor="end">'+t+'</text>'});
var cols={R:'var(--gRun)',C:'var(--gCool)',F:'var(--bad)'},bw=(G.x1-G.x0)/(n-1);
for(i=0;i<n;i++){var col=cols[sb.charAt(i)];if(col)o+='<rect x="'+(X(i)-bw/2).toFixed(1)+'" y="'+(G.y1+5)+'" width="'+(bw+0.4).toFixed(2)+'" height="5" fill="'+col+'"/>'}
var segs=[],cur=null,peak=null;
for(i=0;i<n;i++){
if(c[i]===null){cur=null;continue}
var t=F?c[i]*9/5+32:c[i];if(peak===null||c[i]>peak)peak=c[i];
if(!cur){cur=[];segs.push(cur)}cur.push([X(i),gy(t,max)]);
}
segs.forEach(function(sg){
var p=sg.map(function(q){return q[0].toFixed(1)+','+q[1].toFixed(1)}).join(' ');
if(sg.length==1){o+='<circle class="g-dot" cx="'+sg[0][0].toFixed(1)+'" cy="'+sg[0][1].toFixed(1)+'" r="2.5"/>';return}
o+='<polygon class="g-area" points="'+sg[0][0].toFixed(1)+','+G.y1+' '+p+' '+sg[sg.length-1][0].toFixed(1)+','+G.y1+'"/><polyline class="g-line" points="'+p+'"/>';
});
if(peak===null)o+='<text class="g-empty" x="'+((G.x0+G.x1)/2)+'" y="'+((G.y0+G.y1)/2+4)+'" text-anchor="middle">No chamber readings yet - they log while it runs</text>';
o+='<text class="g-ax" x="'+G.x0+'" y="120">-60 min</text><text class="g-ax" x="'+((G.x0+G.x1)/2)+'" y="120" text-anchor="middle">-30</text><text class="g-ax" x="'+G.x1+'" y="120" text-anchor="end">now</text>';
o+='<g id="gHov" style="display:none"><line class="g-x" id="gX" y1="'+G.y0+'" y2="'+G.y1+'"/><circle class="g-dot" id="gD" r="4"/><text class="g-tip" id="gT" y="'+(G.y0+9)+'"></text></g>';
g.innerHTML=o;
$('gPeak').textContent=peak===null?'':'peak '+Math.round(F?peak*9/5+32:peak)+'°'+H.unit;
}
function hover(e){
var hv=$('gHov');if(!hv||!H.hist)return;
if(!e){hv.style.display='none';return}
var g=$('cGraph'),r=g.getBoundingClientRect(),c=H.hist.c,n=c.length;
var x=(e.clientX-r.left)*340/r.width,k=Math.round((x-G.x0)/(G.x1-G.x0)*(n-1));
if(k<0||k>=n){hv.style.display='none';return}
var F=H.unit=='F',px=G.x0+(G.x1-G.x0)*k/(n-1),ago=Math.round((n-1-k)*(H.hist.step||15)/60);
var v=c[k],txt=(ago?'-'+ago+' min':'now')+'  '+(v===null?'no reading':Math.round(F?v*9/5+32:v)+'°'+H.unit);
hv.style.display='';$('gX').setAttribute('x1',px);$('gX').setAttribute('x2',px);
var d=$('gD');if(v===null)d.style.display='none';else{d.style.display='';d.setAttribute('cx',px);d.setAttribute('cy',gy(F?v*9/5+32:v,F?500:250))}
var t=$('gT');t.textContent=txt;var right=px>G.x1-110;t.setAttribute('x',right?px-6:px+6);t.setAttribute('text-anchor',right?'end':'start');
}
function tick(){fetch('/api/status').then(function(r){return r.json()}).then(function(j){H.j=j;if(Date.now()>H.unitHold&&j.unit)H.unit=j.unit;render()}).catch(function(){})}
function hist(){fetch('/api/history').then(function(r){return r.json()}).then(function(h){H.hist=h;drawGraph()}).catch(function(){})}
W.tick=tick;
build();render();tick();hist();
setInterval(tick,2000);setInterval(hist,15000);
})();
)PG";
