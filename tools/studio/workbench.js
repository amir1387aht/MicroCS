/* Studio-only workbench actions. No device polling, downloads or dependencies. */
(() => {
  "use strict";
  const bg = $("commandBg"), input = $("commandInput"), list = $("commandList");
  let previousFocus = null, rows = [], selected = 0, mode = "commands";
  const escape = value => String(value).replace(/[&<>"']/g, c => ({"&":"&amp;","<":"&lt;",">":"&gt;",'"':"&quot;","'":"&#39;"}[c]));
  function sidebar(show) {
    $("main").classList.toggle("sidebar-hidden", !show);
    $("btnExplorer").setAttribute("aria-expanded", String(show)); store.set("explorerVisible", show);
  }
  function updateFormatButton() {
    const tab = App.tab();
    $("btnFormat").disabled = !tab || !!tab.readOnly || !!tab.binary || !/\.cs$/i.test(tab.path || tab.name || "");
  }
  function actions() {
    const tab = App.tab(), text = tab && !tab.binary;
    const enabled = id => !$(id).disabled;
    if (mode === "files") return App.tabs.map(t => ({label:t.path || t.name || "Untitled", detail:t.dirty ? "Unsaved changes" : "Open file", run:()=>App.activate(t.id)}));
    return [
      {label:"File: New C# file",key:"Alt+N",run:()=>App.newScratch()},
      {label:"File: Open from computer",key:"Ctrl+O",run:()=>$("openLocal").click()},
      {label:"File: Switch open file",key:"Ctrl+P",run:()=>open("files")},
      {label:"File: Save and build",key:"Ctrl+S",disabled:!enabled("btnSave"),run:()=>App.save()},
      {label:"Editor: Format document",key:"Shift+Alt+F",disabled:!enabled("btnFormat"),run:()=>ed.formatDocument()},
      {label:"Editor: Find",key:"Ctrl+F",disabled:!text,run:()=>ed.openFind(false)},
      {label:"Editor: Go to line",key:"Ctrl+G",disabled:!text,run:()=>App.gotoLineDialog()},
      {label:"Editor: Suggest completions",key:"Ctrl+Space",disabled:!text,run:()=>{ed.ta.focus();ed.suggest(true);}},
      {label:"Device: Connect / disconnect",run:()=>App.connect()},
      {label:"Run: Active file",key:"Ctrl+Enter",disabled:!enabled("btnRunFile"),run:()=>App.runCurrent(false,true)},
      {label:"Run: File or project",key:"F5",disabled:!enabled("btnRun"),run:()=>App.runCurrent(false)},
      {label:"Run: Stop",disabled:!enabled("btnStop"),run:()=>App.stop()},
      {label:"View: Toggle terminal",key:"Ctrl+`",run:()=>App.toggleTerminal()},
      {label:"View: Toggle explorer",key:"Ctrl+B",run:()=>sidebar($("main").classList.contains("sidebar-hidden"))},
      {label:"View: Toggle light / dark theme",run:()=>App.theme(true)},
      {label:"Templates: Browse examples",key:"Alt+T",run:()=>App.gallery()},
      {label:"Project: Configure",run:()=>App.configureProject()},
    ];
  }
  function render() {
    const terms=input.value.toLowerCase().trim().split(/\s+/).filter(Boolean);
    rows=actions().filter(a=>terms.every(t=>(a.label+" "+(a.detail||"")).toLowerCase().includes(t)));
    selected=Math.max(0,Math.min(selected,rows.length-1));
    list.innerHTML=rows.length ? rows.map((a,i)=>`<div id="command-${i}" role="option" aria-selected="${i===selected}" aria-disabled="${!!a.disabled}" data-index="${i}" class="command-row${i===selected?" selected":""}${a.disabled?" disabled":""}"><span>${escape(a.label)}${a.detail?`<small>${escape(a.detail)}</small>`:""}</span>${a.key?`<kbd>${escape(a.key)}</kbd>`:""}</div>`).join("") : '<div class="command-empty">No matching '+(mode==="files"?"open files":"commands")+'</div>';
    if(rows.length) {input.setAttribute("aria-activedescendant","command-"+selected);$("command-"+selected).scrollIntoView({block:"nearest"});}
    else input.removeAttribute("aria-activedescendant");
  }
  function close(restore=true) {bg.hidden=true;input.setAttribute("aria-expanded","false");if(restore && previousFocus?.isConnected)previousFocus.focus();}
  function open(next="commands") {
    if(bg.hidden) previousFocus=document.activeElement;
    mode=next;selected=0;input.value="";bg.hidden=false;
    input.placeholder=mode==="files"?"Search open files…":"Search commands…";
    input.setAttribute("aria-label",mode==="files"?"Search open files":"Search commands");
    input.setAttribute("aria-expanded","true");render();input.focus();
  }
  async function execute(index) {
    const action=rows[index];if(!action||action.disabled)return;
    close();try{await action.run();}catch(e){toast(e.message||String(e));}
  }
  input.oninput=()=>{selected=0;render();};
  list.onclick=e=>{const row=e.target.closest("[data-index]");if(row)execute(+row.dataset.index);};
  bg.onkeydown=e=>{
    e.stopPropagation(); // Do not run global F5/save actions behind the dialog.
    if(e.key==="Escape"){e.preventDefault();e.stopPropagation();close();}
    else if(["ArrowDown","ArrowUp","Home","End"].includes(e.key)){
      e.preventDefault();selected=e.key==="Home"?0:e.key==="End"?rows.length-1:(selected+(e.key==="ArrowDown"?1:rows.length-1))%Math.max(1,rows.length);render();
    }else if(e.key==="Enter"){e.preventDefault();execute(selected);}
    else if(e.key==="Tab"){e.preventDefault();(document.activeElement===input?$("commandClose"):input).focus();}
  };
  bg.onclick=e=>{if(e.target===bg)close();};$("commandClose").onclick=()=>close();
  $("btnCommands").onclick=()=>open();$("btnExplorer").onclick=()=>sidebar($("main").classList.contains("sidebar-hidden"));
  $("btnFormat").onclick=()=>ed.formatDocument();
  document.addEventListener("keydown",e=>{
    if((e.ctrlKey||e.metaKey)&&!e.altKey&&(e.key.toLowerCase()==="p"||!e.shiftKey&&e.key.toLowerCase()==="b")){
      if(!$("modalBg").classList.contains("show")&&!$("galleryBg").classList.contains("show")){
        e.preventDefault();e.stopImmediatePropagation();
        if(e.key.toLowerCase()==="b")sidebar($("main").classList.contains("sidebar-hidden"));else open(e.shiftKey?"commands":"files");
      }
    }
  },true);
  document.addEventListener("studio:tabs",updateFormatButton);
  document.querySelectorAll("button[title]").forEach(b=>{if(!b.hasAttribute("aria-label"))b.setAttribute("aria-label",b.title);});
  sidebar(store.get("explorerVisible",innerWidth>720));updateFormatButton();
})();
