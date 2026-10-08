import {expect, test, type Page} from '@playwright/test';

test.beforeEach(async ({page}) => {
  await page.addInitScript(() => {
    const png = (width:number, height:number, color = '#647888') => {
      const canvas = document.createElement('canvas');
      canvas.width = width; canvas.height = height;
      const ctx = canvas.getContext('2d')!;
      ctx.fillStyle = color; ctx.fillRect(0, 0, width, height);
      return canvas.toDataURL('image/png').split(',')[1];
    };
    const fast = png(1200, 801), high = png(1290, 860), thumbnail = png(400, 267);
    let finishOverview: (()=>void) | undefined;
    const decodes = new Set<()=>void>();
    const decode = HTMLImageElement.prototype.decode;
    const state = {holdOverview:false, holdDetail:false, details:[] as any[],
      get pendingDecodes() { return decodes.size; },
      finishOverview:()=>finishOverview?.(), finishDetails:()=>{
        state.holdDetail = false;
        for(const finish of decodes) finish();
        decodes.clear();
      }};
    HTMLImageElement.prototype.decode = function() {
      const ready = decode.call(this);
      if(state.holdDetail && state.details.some(detail=>detail.url===this.src)) {
        const held = new Promise<void>(resolve=>decodes.add(resolve));
        return Promise.all([ready,held]).then(()=>{});
      }
      return ready;
    };
    Object.assign(window,{isTauri:true,__viewport:state,__TAURI_INTERNALS__:{invoke:async(command:string,args:any)=>{
      if(command==='external_session'||command==='supersede_previews') return null;
      if(command==='raw_source_version') return 'v1';
      if(command==='processing_models') return {models:{denoise:[],sharpen:[],provider:'cpu'}};
      if(command==='render_raw_preview') {
        if(!args.background && state.holdOverview) await new Promise<void>(resolve=>{finishOverview=resolve;});
        return {version:1,width:args.highQuality?1290:1200,height:args.highQuality?860:801,
          sourceWidth:6000,sourceHeight:4000,sourceVersion:'v1',quality:args.highQuality?'high':'fast',
          png:args.background?thumbnail:args.highQuality?high:fast,metadata:{rawWidth:6000,rawHeight:4000}};
      }
      if(command==='render_raw_detail') {
        const pixels = `${png(args.width,args.height,`hsl(${state.details.length * 30},30%,50%)`)}#${state.details.length}`;
        const url = `data:image/png;base64,${pixels}`;
        state.details.push({...args,url});
        return {...args,version:1,sourceWidth:6000,sourceHeight:4000,sourceVersion:'v1',png:pixels};
      }
      if(command==='desktop_control') {
        const op=args.request.operation;
        if(op==='library_list') return {paths:['/photos/a.ARW']};
        if(op==='preset_list') return {names:[]};
        if(op==='recipe_load') return {outcome:'missing',recipe:null};
        if(op==='recipe_save') return {saved:true};
        if(op==='library_metadata'||op==='library_metadata_save') return {rating:0,labels:[]};
      }
      throw new Error(`Unexpected ${command}`);
    }}});
  });
  await page.goto('/');
  await page.getByRole('button',{name:'Select a.ARW',exact:true}).dblclick();
  await expect.poll(()=>page.locator('.main-photo').evaluate((img:HTMLImageElement)=>img.naturalWidth)).toBe(1200);
});

async function viewport(page:Page) {
  return page.locator('.photo-frame').evaluate(frame=>{
    const image=frame.getBoundingClientRect(), canvas=frame.parentElement!.getBoundingClientRect();
    const matrix=new DOMMatrix(getComputedStyle(frame).transform);
    return {width:image.width,height:image.height,x:image.x,y:image.y,tx:matrix.e,ty:matrix.f,
      scale:image.width/6000,
      centerX:(canvas.x+canvas.width/2-image.x)/image.width,
      centerY:(canvas.y+canvas.height/2-image.y)/image.height};
  });
}

test('1:1 preserves the viewed point and zoom controls use the actual magnification', async ({page}) => {
  const one=page.getByRole('button',{name:'1:1',exact:true});
  await one.click();
  const canvas=(await page.locator('.canvas').boundingBox())!;
  const x=Math.round(canvas.x+canvas.width/2), y=Math.round(canvas.y+canvas.height/2);
  await page.mouse.move(x,y); await page.mouse.down();
  await page.mouse.move(x+120,y+75,{steps:5}); await page.mouse.up();
  const panned=await viewport(page);
  await one.click(); await one.click();
  expect(await viewport(page)).toEqual(panned);
  await expect(one).toHaveAttribute('aria-pressed','true');
  await page.getByRole('button',{name:'Zoom in',exact:true}).click();
  const enlarged=await viewport(page);
  expect(enlarged.scale).toBeCloseTo(1.25,5);
  expect(enlarged.centerX).toBeCloseTo(panned.centerX,5);
  expect(enlarged.centerY).toBeCloseTo(panned.centerY,5);
  await expect(page.locator('.zoom')).toHaveText('125%');
  await expect(one).toHaveAttribute('aria-pressed','false');
  await page.getByRole('button',{name:'Zoom out',exact:true}).click();
  expect((await viewport(page)).scale).toBeCloseTo(1,5);
  await page.mouse.move(x+100,y+30);
  const before=await viewport(page);
  await page.keyboard.down('Control'); await page.mouse.wheel(0,-200); await page.keyboard.up('Control');
  await expect.poll(async ()=>(await viewport(page)).scale).toBeGreaterThan(1);
  const after=await viewport(page);
  expect((x+100-after.x)/after.width).toBeCloseTo((x+100-before.x)/before.width,5);
  expect((y+30-after.y)/after.height).toBeCloseTo((y+30-before.y)/before.height,5);
  await expect(page.locator('.zoom')).toHaveText(`${Math.round(after.scale*100)}%`);
  await expect(one).toHaveAttribute('aria-pressed','false');
  await one.click();
  const actual=await viewport(page);
  expect(actual.scale).toBeCloseTo(1,5);
  expect(actual.centerX).toBeCloseTo(after.centerX,5);
  expect(actual.centerY).toBeCloseTo(after.centerY,5);
  await page.mouse.move(x,y); await page.mouse.down();
  await page.mouse.move(x+40,y,{steps:3});
  await page.keyboard.down('Control'); await page.mouse.wheel(0,-200); await page.keyboard.up('Control');
  await expect.poll(async ()=>(await viewport(page)).scale).toBeGreaterThan(1);
  const wheelDuringDrag=await viewport(page);
  await page.mouse.up();
  expect(await viewport(page)).toEqual(wheelDuringDrag);
  await page.getByRole('button',{name:'Fit image',exact:true}).click();
  expect((await viewport(page)).centerX).toBeCloseTo(.5,5);
  expect((await viewport(page)).centerY).toBeCloseTo(.5,5);
});

test('late overview and detail images do not move or stretch a dragged viewport', async ({page}) => {
  await page.getByRole('button',{name:'1:1',exact:true}).click();
  await expect(page.getByAltText('Source-resolution detail')).toBeVisible();
  const firstX=await page.evaluate(()=>(window as any).__viewport.details.at(-1).x);
  await page.evaluate(()=>(window as any).__viewport.holdOverview=true);
  await page.getByRole('switch',{name:'High Quality'}).click();
  const canvas=(await page.locator('.canvas').boundingBox())!;
  const x=canvas.x+canvas.width/2, y=canvas.y+canvas.height/2;
  await page.mouse.move(x,y); await page.mouse.down();
  await page.mouse.move(x+150,y+90,{steps:5});
  const dragging=await viewport(page);
  expect(dragging.width).toBe(6000);
  expect(dragging.height).toBe(4000);
  await page.evaluate(()=>(window as any).__viewport.finishOverview());
  await expect.poll(()=>page.locator('.main-photo').evaluate((img:HTMLImageElement)=>img.naturalWidth)).toBe(1290);
  expect(await viewport(page)).toEqual(dragging);
  await page.mouse.up();
  expect(await viewport(page)).toEqual(dragging);
  await expect.poll(()=>page.evaluate(()=>(window as any).__viewport.details.at(-1)?.x)).toBe(firstX-150);
  await expect(page.getByAltText('Source-resolution detail')).toHaveAttribute('src',await page.evaluate(()=>(window as any).__viewport.details.at(-1).url));
  const tile=page.getByAltText('Source-resolution detail',{exact:true});
  const previous={src:await tile.getAttribute('src'),box:await tile.boundingBox()};
  const nextX=await page.evaluate(()=>(window as any).__viewport.details.at(-1).x-70);
  await page.evaluate(()=>(window as any).__viewport.holdDetail=true);
  await page.mouse.move(x,y); await page.mouse.down();
  await page.mouse.move(x+70,y,{steps:4}); await page.mouse.up();
  await expect.poll(()=>page.evaluate(()=>(window as any).__viewport.details.at(-1).x)).toBe(nextX);
  await expect.poll(()=>page.evaluate(()=>(window as any).__viewport.pendingDecodes)).toBeGreaterThan(0);
  await expect(tile).toHaveAttribute('src',previous.src!);
  expect((await tile.boundingBox())!.x).toBeCloseTo(previous.box!.x+70,4);
  const replacement=await page.evaluate(()=>{
    const {x,y,url}=(window as any).__viewport.details.at(-1);
    return {x,y,id:url.split('#')[1]};
  });
  await page.mouse.move(x,y); await page.mouse.down();
  await page.mouse.move(x+30,y+20,{steps:3});
  const midDrag=await viewport(page);
  await page.evaluate(()=>(window as any).__viewport.finishDetails());
  await expect.poll(async ()=>(await tile.getAttribute('src'))?.split('#')[1]).toBe(replacement.id);
  expect(await viewport(page)).toEqual(midDrag);
  expect((await tile.boundingBox())!.x).toBeCloseTo(midDrag.x+replacement.x,4);
  expect((await tile.boundingBox())!.y).toBeCloseTo(midDrag.y+replacement.y,4);
  await page.mouse.up();
  expect(await viewport(page)).toEqual(midDrag);
  await page.getByRole('button',{name:'Before and after'}).click();
  await expect(page.getByAltText('Original source-resolution detail')).toBeVisible();
  expect(await page.getByAltText('Original source-resolution detail').boundingBox()).toEqual(await tile.boundingBox());
  expect(await viewport(page)).toEqual(midDrag);
});
