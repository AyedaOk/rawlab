import {expect, test} from '@playwright/test';

const png = 'iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+jRZkAAAAASUVORK5CYII=';

test('the latest active photograph owns the visible preview', async ({page}) => {
  await page.addInitScript((png) => {
    const active = new Map<string,(value:unknown)=>void>();
    const calls:any[]=[];
    Object.assign(window,{isTauri:true,__previewCalls:calls,
      __resolveActive:(path:string) => active.get(path)?.({version:1,width:100,height:80,sourceWidth:6000,sourceHeight:4000,png:`${png}${path.endsWith('b.ARW')?'B':'A'}`,quality:'high',metadata:{model:path,rawWidth:6000,rawHeight:4000}}),
      __TAURI_INTERNALS__:{invoke:async(command:string,args:any)=>{
        if(command==='external_session') return null;
       if(command==='processing_models') return {models:{denoise:[{id:'hf:rawdenoise-okeinla',name:'Okeinla',available:true}],sharpen:[],provider:'cpu'}};
        if(command==='render_raw_preview') {
          calls.push(structuredClone(args));
          if(args.background) return {version:1,width:100,height:80,sourceWidth:6000,sourceHeight:4000,png,quality:'fast',metadata:{model:args.path,rawWidth:6000,rawHeight:4000}};
          return new Promise((resolve)=>active.set(args.path,resolve));
        }
        if(command==='desktop_control') {
          const request=args.request;
          if(request.operation==='library_list') return {paths:['/photos/a.ARW','/photos/b.ARW']};
          if(request.operation==='preset_list') return {names:[]};
          if(request.operation==='recipe_load') return {outcome:'missing',recipe:null};
          if(request.operation==='recipe_save') return {saved:true};
          if(request.operation==='library_metadata'||request.operation==='library_metadata_save') return {rating:0,labels:[]};
        }
        throw new Error(`Unexpected native command: ${command}`);
      }}});
  },png);
  await page.goto('/');
  await expect(page.getByRole('button',{name:'Select a.ARW',exact:true})).toBeVisible();
  await page.getByRole('button',{name:'Select a.ARW',exact:true}).dblclick();
  await expect.poll(() => page.evaluate(() => (window as any).__previewCalls.filter((call:any)=>!call.background&&call.path.endsWith('a.ARW')).length)).toBe(1);
  await page.getByRole('button',{name:'Edit b.ARW',exact:true}).click();
  await expect.poll(() => page.evaluate(() => (window as any).__previewCalls.filter((call:any)=>!call.background&&call.path.endsWith('b.ARW')).length)).toBe(1);
  await page.evaluate(() => (window as any).__resolveActive('/photos/b.ARW'));
  await page.evaluate(() => (window as any).__resolveActive('/photos/a.ARW'));
  await expect(page.locator('.main-photo')).toHaveAttribute('src',/B$/);
  expect(await page.evaluate(() => (window as any).__previewCalls.some((call:any)=>call.background===true))).toBe(true);
});

test('quality changes issue one source request and reuse processed derivatives', async ({page}) => {
  await page.addInitScript((png) => {
    const calls:any[]=[];
    let resolveFirst!:(value:unknown)=>void;
    let processedCalls=0;
    let sourceVersion='v1';
    Object.assign(window,{isTauri:true,__previewCalls:calls,
      __setSourceVersion:(value:string)=>{sourceVersion=value;},
      __resolveProcessed:()=>resolveFirst({version:1,width:100,height:80,sourceWidth:6000,sourceHeight:4000,sourceVersion:'v1',png:`${png}F`,quality:'fast',metadata:{rawWidth:6000,rawHeight:4000}}),
      __TAURI_INTERNALS__:{invoke:async(command:string,args:any)=>{
      if(command==='external_session') return null;
      calls.push({command,args:structuredClone(args)});
      if(command==='processing_models') return {models:{denoise:[{id:'hf:rawdenoise-okeinla',name:'Okeinla',available:true}],sharpen:[],provider:'cpu'}};
      if(command==='raw_source_version') return sourceVersion;
      if(command==='render_raw_preview') return {version:1,width:100,height:80,sourceWidth:6000,sourceHeight:4000,sourceVersion,png,quality:args.highQuality?'high':'fast',metadata:{rawWidth:6000,rawHeight:4000}};
      if(command==='process_raw') return {version:1,width:100,height:80,sourceWidth:6000,sourceHeight:4000,sourceVersion:'v1',resultId:'result-1',png,before:png,quality:'high',metadata:{rawWidth:6000,rawHeight:4000}};
      if(command==='render_processed') {
        processedCalls++;
        if(processedCalls===1) return new Promise((resolve)=>{resolveFirst=resolve;});
        return {version:1,width:100,height:80,sourceWidth:6000,sourceHeight:4000,sourceVersion:'v1',png:`${png}${args.highQuality?'H':'F'}`,quality:args.highQuality?'high':'fast',metadata:{rawWidth:6000,rawHeight:4000}};
      }
      if(command==='desktop_control') {
        const request=args.request;
        if(request.operation==='library_list') return {paths:['/photos/a.ARW']};
        if(request.operation==='preset_list') return {names:[]};
        if(request.operation==='recipe_load') return {outcome:'missing',recipe:null};
        if(request.operation==='recipe_save') return {saved:true};
        if(request.operation==='library_metadata'||request.operation==='library_metadata_save') return {rating:0,labels:[]};
      }
      throw new Error(`Unexpected native command: ${command}`);
    }}});
  },png);
  await page.goto('/');
  await page.getByRole('button',{name:'Select a.ARW',exact:true}).dblclick();
  await page.getByRole('button',{name:'Update',exact:true}).click();
  await expect(page.getByText('Processing complete.',{exact:false})).toBeVisible();
  await page.evaluate(() => ((window as any).__previewCalls.length=0));
  await page.getByRole('slider',{name:'Blend with denoised'}).fill('55');
  await expect.poll(() => page.evaluate(() => (window as any).__previewCalls.filter((call:any)=>call.command==='process_raw'&&call.args.operation==='recompose').length)).toBe(1);
  expect(await page.evaluate(() => {
    const call=(window as any).__previewCalls.find((item:any)=>item.command==='process_raw'&&item.args.operation==='recompose');
    return call.args.previewSize >= 128 && call.args.previewSize <= 4096
      && call.args.resultId === 'result-1';
  })).toBe(true);
  await page.evaluate(() => ((window as any).__previewCalls.length=0));
  const quality=page.getByRole('switch',{name:'High Quality'});
  await quality.click();
  await expect.poll(() => page.evaluate(() => (window as any).__previewCalls.filter((call:any)=>call.command==='render_processed').length)).toBe(1);
  await expect(page.getByRole('button',{name:'Update',exact:true})).toHaveCount(0);
  expect(await page.evaluate(() => (window as any).__previewCalls.filter((call:any)=>call.command==='render_raw_preview'&&!call.args.background).length)).toBe(1);
  await quality.click();
  await quality.click();
  await page.evaluate(() => (window as any).__resolveProcessed());
  await expect(page.locator('.main-photo')).toHaveAttribute('src',/F$/);
  expect(await page.evaluate(() => (window as any).__previewCalls.filter((call:any)=>call.command==='render_processed').length)).toBe(1);
  await quality.click();
  await expect.poll(() => page.evaluate(() => (window as any).__previewCalls.filter((call:any)=>call.command==='render_processed').length)).toBe(2);
  await quality.click();
  await expect(page.locator('.main-photo')).toHaveAttribute('src',/F$/);
  expect(await page.evaluate(() => (window as any).__previewCalls.filter((call:any)=>call.command==='render_processed').length)).toBe(2);
  await page.getByRole('button',{name:'Linear RGB',exact:true}).click();
  await page.getByRole('button',{name:'Normal',exact:true}).click();
  await expect(page.getByRole('button',{name:'Update',exact:true})).toHaveCount(0);
  await page.evaluate(() => ((window as any).__previewCalls.length=0));
  await page.getByRole('slider',{name:'Strength'}).fill('65');
  await expect.poll(() => page.evaluate(() => (window as any).__previewCalls.filter((call:any)=>call.command==='process_raw'&&call.args.operation==='recompose').length)).toBe(1);
  await expect(page.getByRole('button',{name:'Update',exact:true})).toHaveCount(0);
  await page.evaluate(() => ((window as any).__previewCalls.length=0));
  await page.getByRole('slider',{name:'Strength'}).fill('70');
  await expect.poll(() => page.evaluate(() => (window as any).__previewCalls.filter((call:any)=>call.command==='process_raw'&&call.args.operation==='recompose').length)).toBe(1);
  await page.evaluate(() => (window as any).__setSourceVersion('v2'));
  await page.getByRole('button',{name:'Back to library'}).click();
  await page.getByRole('button',{name:'Select a.ARW',exact:true}).dblclick();
  await expect.poll(() => page.evaluate(() => (window as any).__previewCalls.filter((call:any)=>call.command==='render_raw_preview'&&!call.args.background).length)).toBe(1);
  await expect(page.getByRole('button',{name:'Update',exact:true})).toBeVisible();
});

test('Normal sharpening starts automatically and keeps the detail tile during strength changes', async ({page}) => {
  await page.addInitScript((png) => {
    const calls:any[]=[];
    let delayed = false;
    let finish: (()=>void) | undefined;
    const image = {version:1,width:100,height:80,sourceWidth:6000,sourceHeight:4000,sourceVersion:'v1',png,before:png,resultId:'r1',metadata:{rawWidth:6000,rawHeight:4000}};
    Object.assign(window,{isTauri:true,__sharpCalls:calls,__delayDetail:()=>{delayed=true;},__finishDetail:()=>{delayed=false;finish?.();},
      __TAURI_INTERNALS__:{invoke:async(command:string,args:any)=>{
        calls.push({command,args:structuredClone(args)});
        if(command==='external_session') return null;
        if(command==='raw_source_version') return 'v1';
        if(command==='processing_models') return {models:{denoise:[{id:'hf:rawdenoise-okeinla',name:'Okeinla',available:true}],sharpen:[],provider:'cpu'}};
        if(command==='render_raw_preview'||command==='render_processed'||command==='process_raw') return {...image,quality:args.highQuality?'high':'fast'};
        if(command==='render_raw_detail') {
          if(delayed && args.target==='processed') await new Promise<void>(resolve=>{finish=resolve;});
          return {...image,x:args.x,y:args.y,width:args.width,height:args.height};
        }
        if(command==='desktop_control') {
          const op=args.request.operation;
          if(op==='library_list')return {paths:['/photos/a.ARW']};
          if(op==='preset_list')return {names:[]};
          if(op==='recipe_load')return {outcome:'missing',recipe:null};
          if(op==='recipe_save')return {saved:true};
          if(op==='library_metadata'||op==='library_metadata_save')return {rating:0,labels:[]};
        }
        throw new Error(`Unexpected ${command}`);
      }}});
  },png);
  await page.goto('/');
  await page.getByRole('button',{name:'Select a.ARW',exact:true}).dblclick();
  await page.getByRole('button',{name:'Linear RGB',exact:true}).click();
  await page.getByRole('button',{name:'Normal',exact:true}).click();
  await expect(page.getByRole('button',{name:'Update',exact:true})).toHaveCount(0);
  await page.getByRole('button',{name:'1:1',exact:true}).click();
  await expect(page.locator('.detail-tile').first()).toBeVisible();
  await page.evaluate(()=>{(window as any).__sharpCalls.length=0;(window as any).__delayDetail();});
  await page.getByRole('slider',{name:'Strength',exact:true}).fill('65');
  await expect.poll(()=>page.evaluate(()=>(window as any).__sharpCalls.filter((c:any)=>c.command==='render_raw_detail'&&c.args.target==='processed').length)).toBe(1);
  await expect(page.locator('.detail-tile').first()).toBeVisible();
  await page.getByRole('slider',{name:'Strength',exact:true}).fill('75');
  await page.getByRole('slider',{name:'Strength',exact:true}).fill('85');
  await page.evaluate(()=>(window as any).__finishDetail());
  await expect.poll(()=>page.evaluate(()=>(window as any).__sharpCalls.filter((c:any)=>c.command==='render_raw_detail'&&c.args.target==='processed').at(-1)?.args.recipe.layers[0].strength)).toBe(85);
  expect(await page.evaluate(()=>(window as any).__sharpCalls.filter((c:any)=>c.command==='render_raw_detail'&&c.args.target==='original').length)).toBe(0);
  expect(await page.evaluate(()=>(window as any).__sharpCalls.filter((c:any)=>c.command==='process_raw'&&c.args.operation!=='recompose').length)).toBe(0);
});

test('wheel zoom uses oriented source dimensions and requests a bounded detail region', async ({page}) => {
  await page.setViewportSize({width:3840,height:2160});
  await page.addInitScript((png) => {
    const details:any[]=[];
    Object.assign(window,{isTauri:true,__details:details,__TAURI_INTERNALS__:{invoke:async(command:string,args:any)=>{
      if(command==='external_session') return null;
      if(command==='processing_models') return {models:{denoise:[{id:'denoise',name:'Denoise',available:true}],sharpen:[],provider:'cpu'}};
      if(command==='render_raw_preview') return {version:1,width:1400,height:934,sourceWidth:7008,sourceHeight:4672,sourceVersion:'v1',png,quality:'high',metadata:{rawWidth:7008,rawHeight:4672}};
      if(command==='render_raw_detail') {details.push(structuredClone(args));return {version:1,x:args.x,y:args.y,width:args.width,height:args.height,sourceWidth:7008,sourceHeight:4672,png};}
      if(command==='desktop_control') {
        const request=args.request;
        if(request.operation==='library_list') return {paths:['/photos/a.ARW']};
        if(request.operation==='preset_list') return {names:[]};
        if(request.operation==='recipe_load') return {outcome:'missing',recipe:null};
        if(request.operation==='recipe_save') return {saved:true};
        if(request.operation==='library_metadata'||request.operation==='library_metadata_save') return {rating:0,labels:[]};
      }
      throw new Error(`Unexpected native command: ${command}`);
    }}});
  },png);
  await page.goto('/');
  await page.getByRole('button',{name:'Select a.ARW',exact:true}).dblclick();
  await page.locator('.canvas').hover();
  await page.keyboard.down('Control');
  await page.mouse.wheel(0,-300);
  await page.keyboard.up('Control');
  await expect.poll(() => page.evaluate(() => (window as any).__details.length)).toBe(1);
  await expect(page.locator('.photo-frame')).toHaveCSS('width','7008px');
  await expect(page.getByAltText('Source-resolution detail')).toBeVisible();
   const request=await page.evaluate(() => (window as any).__details[0]);
   expect(request.target).toBe('original');
   expect(request.recipe.outputKind).toBe('cfa');
  expect(request.width*request.height).toBeGreaterThan(4*1024*1024);
  expect(request.width*request.height).toBeLessThanOrEqual(16*1024*1024);
  expect(request.width).toBeLessThan(7008);
});
