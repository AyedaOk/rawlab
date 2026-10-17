import { test, expect } from '@playwright/test';

test('desktop import uses native bridge and handles cancellation and read failures', async ({ page }) => {
  await page.addInitScript(() => {
    let selections = 0;
    Object.assign(window, { isTauri: true, __TAURI_INTERNALS__: {
      invoke: async (command: string, args: Record<string, any>) => {
        if (command === 'external_session' || command === 'supersede_previews') return null;
        if (command === 'plugin:dialog|open') {
          selections++;
          return selections === 1 ? null : ['/photos/native.png', '/photos/unreadable.png'];
        }
        if (command === 'plugin:fs|stat') return {size: 64, mtime: null};
        if (command === 'plugin:fs|read_file') {
          if(args.path.endsWith('unreadable.png')) throw new Error('Permission denied');
          return Array.from(atob('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+jRZkAAAAASUVORK5CYII='), c => c.charCodeAt(0));
        }
        if (command === 'plugin:path|basename') return args.path.split('/').pop();
        throw new Error(`Unexpected native command: ${command}`);
      }
    }});
  });
  await page.goto('/');
  await page.getByRole('button', {name: 'Import Pictures', exact:true}).click();
  await expect(page.locator('.photo-card')).toHaveCount(0);
  await expect(page.getByRole('button', {name: 'Edit', exact:true})).toBeDisabled();
  await page.getByRole('button', {name: 'Import Pictures', exact:true}).click();
  await expect(page.getByRole('button', {name: 'Select native.png'})).toBeVisible();
  await expect(page.getByText('1 unsupported, unreadable or oversized entries skipped', {exact:false})).toBeVisible();
  await page.getByRole('button', {name: 'Edit', exact:true}).click();
  await expect(page.locator('.main-photo')).toHaveAttribute('alt', 'native.png');
});

test('RAW thumbnails, metadata and quality changes use the native renderer', async ({ page }) => {
  await page.addInitScript(() => {
    const png = 'iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+jRZkAAAAASUVORK5CYII=';
    const calls:any[]=[];
    Object.assign(window, { isTauri: true, __processingCalls:calls, __TAURI_INTERNALS__: {
      invoke: async (command: string, args: Record<string, any>) => {
        if(command === 'process_raw' || command === 'render_processed') calls.push({command,...structuredClone(args)});
        if (command === 'external_session' || command === 'supersede_previews') return null;
        if(command === 'plugin:dialog|open') {
          (window as any).__importOptions = args.options;
          return ['/photos/camera.ARW'];
        }
        if(command === 'plugin:fs|stat') return {size: 37052416, mtime: null};
        if(command === 'plugin:path|basename') return args.path.split('/').pop();
        if(command === 'render_raw_preview') {
          if(args.highQuality) await new Promise(resolve => setTimeout(resolve, 100));
          return {version:1, width:1, height:1, png, quality:args.highQuality ? 'high':'fast', metadata:{make:'SONY', model:'ILCE-7CM2', lens:'Test lens', iso:640, aperture:2.8, exposureBiasEv:-1, rawWidth:7008, rawHeight:4672, optics:{lensfunAvailable:true,profileMatched:true,chromaticAberrationAvailable:true,lensCorrectionAvailable:true,message:'Test lens'}}};
        }
        if(command === 'processing_models') return {models:{denoise:[{id:'hf:rawdenoise-okeinla',name:'Okeinla',available:true}],sharpen:[],provider:'cpu'}};
        if(command === 'desktop_control') {
          if(args.request.operation === 'recipe_load') return {outcome:'missing',recipe:null};
          if(args.request.operation === 'recipe_save') return {saved:true};
        }
        if(command === 'process_raw') {
          if(args.recipe.layers[0].modelId !== 'hf:rawdenoise-okeinla') throw new Error('Wrong processing model');
          return {version:1,width:1,height:1,png,before:png,resultId:'result-1',output:args.output,quality:'high',metadata:{}};
        }
        if(command === 'render_processed') return {version:1,width:1,height:1,png,quality:'high',metadata:{}};
        throw new Error(`Unexpected native command: ${command}`);
      }
    }});
  });
  await page.goto('/');
  await page.getByRole('button', {name:'Import Pictures',exact:true}).click();
  const card = page.getByRole('button', {name:'Select camera.ARW',exact:true});
  await expect(card.locator('img')).toHaveAttribute('src', /^data:image\/png;base64,/);
  const extensions = await page.evaluate(() => (window as any).__importOptions.filters[0].extensions);
  for (const extension of ['arw','cr2','cr3','nef','nrw','dng','orf','rw2','pef','raf','srw','raw','jpg','jpeg','png','webp']) {
    expect(extensions).toContain(extension);
    expect(extensions).toContain(extension.toUpperCase());
  }
  await expect(page.locator('.photo-card')).toHaveCount(1);
  await card.dblclick();
  await expect(page.locator('.main-photo')).toHaveAttribute('src', /^data:image\/png;base64,/);
  await expect(page.locator('.photo-info')).toContainText('ISO 640');
  await expect(page.locator('.photo-info')).toContainText('-1.0 EV');
   await expect(page.getByRole('switch', {name:'High Quality'})).toHaveAttribute('aria-checked', 'false');
   await page.getByRole('switch', {name:'High Quality'}).click();
   await expect(page.getByRole('switch', {name:'High Quality'})).toHaveAttribute('aria-checked', 'true');
  await expect(page.locator('.raw-progress')).toHaveCount(0);
  await page.getByRole('button', {name:'Refresh RAW preview'}).click();
  await expect(page.locator('.raw-progress')).toHaveCount(0);
  await page.getByRole('button', {name:'Update',exact:true}).click();
  await expect(page.getByText('Processing complete.',{exact:false})).toBeVisible();
  expect(await page.evaluate(() => (window as any).__processingCalls.filter((call:any)=>call.command==='process_raw').at(-1).recipe.outputKind)).toBe('cfa');
  await page.getByRole('button', {name:'Linear RGB'}).click();
  await expect(page.getByRole('button', {name:'Update',exact:true})).toHaveCount(0);
  const processCount = await page.evaluate(() => (window as any).__processingCalls.filter((call:any)=>call.command==='process_raw').length);
  await page.getByRole('group', {name:'Lens correction'}).getByRole('button', {name:'On'}).click();
  await expect.poll(() => page.evaluate(() => (window as any).__processingCalls.filter((call:any)=>call.command==='render_processed').at(-1)?.recipe.lensCorrection)).toBe(true);
  expect(await page.evaluate(() => (window as any).__processingCalls.filter((call:any)=>call.command==='process_raw').length)).toBe(processCount);
  await expect(page.getByRole('button', {name:'Update',exact:true})).toHaveCount(0);
  await page.getByRole('button', {name:'Raw',exact:true}).click();
  await expect.poll(() => page.evaluate(() => (window as any).__processingCalls.filter((call:any)=>call.command==='render_processed').at(-1)?.recipe.outputKind)).toBe('cfa');
  await expect(page.getByRole('button', {name:'Update',exact:true})).toHaveCount(0);
  await page.getByRole('button', {name:'Linear RGB',exact:true}).click();
  await expect(page.getByRole('button', {name:'Update',exact:true})).toHaveCount(0);
  expect(await page.evaluate(() => (window as any).__processingCalls.filter((call:any)=>call.command==='process_raw').length)).toBe(processCount);
  await page.getByRole('button', {name:'Before and after'}).click();
  await expect(page.locator('.after-tag')).toHaveText('After');
  await expect(page.getByAltText('Original before processing')).toHaveAttribute('src',/^data:image\/png/);
  await page.getByRole('button', {name:'Add mask',exact:true}).click();
  await page.getByRole('button', {name:'Edit Mask',exact:true}).click();
  await expect.poll(() => page.evaluate(() => (window as any).__processingCalls.filter((call:any)=>call.command==='render_processed').at(-1)?.recipe.outputKind)).toBe('cfa');
});

test('desktop folder import and removal leave no demo photograph', async ({page}) => {
  const errors: string[] = [];
  page.on('pageerror', error => errors.push(error.message));
  await page.addInitScript(() => {
    const png = 'iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+jRZkAAAAASUVORK5CYII=';
    const removed: string[] = [];
    Object.assign(window, {isTauri:true,__removedPaths:removed,__TAURI_INTERNALS__:{invoke:async(command:string,args:any)=>{
      if(command==='external_session'||command==='supersede_previews') return null;
      if(command==='processing_models') return {models:{denoise:[],sharpen:[],provider:'cpu'}};
      if(command==='plugin:dialog|open') return args.options.directory ? '/photos' : null;
      if(command==='plugin:fs|read_dir') return ['camera.ARW','camera.nef','preview.PNG'].map(name => ({name,isFile:true,isDirectory:false,isSymlink:false}));
      if(command==='plugin:fs|stat') return {size:64,mtime:null};
      if(command==='plugin:fs|read_file') return Array.from(atob(png), c => c.charCodeAt(0));
      if(command==='plugin:path|basename') return args.path.split('/').pop();
      if(command==='plugin:path|join') return args.paths.join('/');
      if(command==='render_raw_preview') return {version:1,width:1,height:1,png,quality:'fast',metadata:{rawWidth:1,rawHeight:1}};
      if(command==='desktop_control') {
        switch(args.request.operation) {
          case 'library_list': return {paths:[]};
          case 'library_add': return {paths:args.request.paths};
          case 'library_remove': removed.push(...args.request.paths); return {paths:[]};
          case 'preset_list': return {names:[]};
          case 'recipe_load': return {outcome:'missing',recipe:null};
          case 'recipe_save': return {saved:true};
          case 'library_metadata': case 'library_metadata_save': return {rating:0,labels:[]};
        }
      }
      return null;
    }}});
  });
  await page.goto('/');
  const cards = page.locator('.photo-card');
  await expect(cards).toHaveCount(0);
  await expect(page.locator('.empty-state')).toHaveText('Import pictures or a folder to begin.');
  await page.getByRole('button',{name:'Import Folder',exact:true}).click();
  await expect(cards).toHaveCount(3);
  await expect(page.getByRole('button',{name:/Alpine lake/})).toHaveCount(0);
  await expect(page.getByRole('button',{name:'Select photos/camera.ARW',exact:true})).toBeVisible();
  await expect(page.getByRole('button',{name:'Select photos/camera.nef',exact:true})).toBeVisible();
  await cards.first().click();
  await cards.last().click({modifiers:['Shift']});
  await page.keyboard.press('Delete');
  await expect(cards).toHaveCount(0);
  await expect(page.getByRole('button',{name:'Edit',exact:true})).toBeDisabled();
  expect(await page.evaluate(() => (window as any).__removedPaths)).toEqual(['/photos/camera.ARW','/photos/camera.nef']);
  await page.reload();
  await expect(cards).toHaveCount(0);
  await page.getByRole('button',{name:'Import Folder',exact:true}).click();
  await expect(cards).toHaveCount(3);
  await page.getByRole('button',{name:'Edit',exact:true}).click();
  await expect(page.locator('.main-photo')).toHaveAttribute('alt','photos/preview.PNG');
  expect(errors).toEqual([]);
});

test('external edit restores depth maps, exports to the reserved path and returns success', async ({page}) => {
  await page.addInitScript(() => {
    const png='iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+jRZkAAAAASUVORK5CYII=';
    const calls:any[]=[];
    Object.assign(window,{isTauri:true,__externalCalls:calls,__TAURI_INTERNALS__:{invoke:async(command:string,args:any)=>{
      calls.push({command,args:structuredClone(args)});
      if(command==='external_session') return {mode:'single',sources:['/photos/a.ARW'],output:'/photos/a-rawlab.dng',recipeSource:'/photos/settings.ARW'};
      if(command==='supersede_previews') return null;
      if(command==='processing_models') return {models:{denoise:[{id:'denoise',name:'Denoise',available:true}],sharpen:[],provider:'cpu'}};
      if(command==='render_raw_preview') return {version:1,width:100,height:80,sourceWidth:100,sourceHeight:80,png,quality:'fast',metadata:{rawWidth:100,rawHeight:80}};
      if(command==='process_raw') return {version:1,width:100,height:80,png,before:png,output:args.output,quality:'fast',metadata:{}};
      if(command==='finish_external') return null;
      if(command==='desktop_control') {
        const request=args.request;
        if(request.operation==='library_list') return {paths:[]};
        if(request.operation==='preset_list') return {names:[]};
        if(request.operation==='recipe_load') return {outcome:'loaded',recipe:{
          layers:[{id:0},{id:1,name:'Restored depth',depth:{map:'original-depth',range:[0,0,50,65]}}],
          depthMaps:{'original-depth':{png,modelId:'small'}}
        }};
        if(request.operation==='recipe_save') return {saved:true};
        if(request.operation==='library_metadata'||request.operation==='library_metadata_save') return {rating:0,labels:[]};
      }
      throw new Error(`Unexpected native command: ${command}`);
    }}});
  });
  await page.goto('/');
  await expect(page.getByRole('button',{name:'Restored depth Local adjustment',exact:true})).toBeVisible();
  await expect(page.getByRole('button',{name:'Export and return to Darktable'})).toBeVisible();
  await page.getByRole('button',{name:'Export and return to Darktable'}).click();
  await expect.poll(() => page.evaluate(() => (window as any).__externalCalls.some((call:any)=>call.command==='finish_external'&&call.args.code===0))).toBe(true);
  expect(await page.evaluate(() => {
    const calls=(window as any).__externalCalls;
    const exported=calls.find((item:any)=>item.command==='process_raw'&&item.args.output);
    const saved=calls.filter((item:any)=>item.command==='desktop_control'&&item.args.request.operation==='recipe_save').at(-1);
    const loaded=calls.find((item:any)=>item.command==='desktop_control'&&item.args.request.operation==='recipe_load');
    const map=exported.args.recipe.depthMaps?.['original-depth'];
    return exported.args.output==='/photos/a-rawlab.dng' && loaded.args.request.path==='/photos/settings.ARW'
      && !!map?.png && map.modelId==='small'
      && exported.args.recipe.layers[1].depth.map==='original-depth'
      && JSON.stringify(saved.args.request.recipe.depthMaps)===JSON.stringify(exported.args.recipe.depthMaps);
  })).toBe(true);
});

for (const mode of ['single', 'batch']) test(`Darktable ${mode} waits for failed saves and retries completion without re-exporting`, async ({page}) => {
  await page.addInitScript((mode) => {
    const png='iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAAC0lEQVR4nGP4DwQACfsD/fteaysAAAAASUVORK5CYII=';
    let release: (()=>void) | undefined;
    Object.assign(window,{isTauri:true,__failSave:true,__exports:0,__exits:[],__finishExport:()=>release?.(),
      __TAURI_INTERNALS__:{invoke:async(command:string,args:any)=>{
        const state=window as any;
        if(command==='external_session') return {mode,sources:['/photos/a.ARW'],output:'/photos/a-rawlab.dng',request:'/photos/request.json'};
        if(command==='supersede_previews') return null;
        if(command==='processing_models') return {models:{denoise:[{id:'denoise',name:'Denoise',available:true}],sharpen:[],provider:'cpu'}};
        if(command==='render_raw_preview') return {version:1,width:100,height:80,sourceWidth:100,sourceHeight:80,png,quality:'fast',metadata:{rawWidth:100,rawHeight:80}};
        if(command==='process_raw'||command==='run_external_batch') {
          state.__exports++;
          state.__exportedRecipe=structuredClone(args.recipe);
          await new Promise<void>(resolve=>{release=resolve;});
          return {version:1,width:100,height:80,png,before:png,output:args.output,complete:true};
        }
        if(command==='finish_external') {state.__exits.push(args.code);return null;}
        if(command==='desktop_control') {
          const op=args.request.operation;
          if(op==='library_list') return {paths:[]};
          if(op==='preset_list') return {names:[]};
          if(op==='recipe_load') return {outcome:'missing',recipe:null};
          if(op==='recipe_save') {
            if(state.__failSave) throw new Error('disk full');
            (state.__recipeSaves??=[]).push(structuredClone(args.request));
            return {saved:true};
          }
          if(op==='library_metadata') return {rating:0,labels:[]};
          if(op==='library_metadata_save') {if(state.__failMeta) throw new Error('disk full');return {rating:args.request.rating,labels:[]};}
        }
        throw new Error(`Unexpected native command: ${command}`);
      }}});
  }, mode);
  await page.goto('/');
  const exportButton=page.getByRole('button',{name:/Export.*Darktable/});
  await exportButton.click();
  await expect(page.getByRole('button',{name:'Retry save'})).toBeVisible();
  expect(await page.evaluate(()=>(window as any).__exports)).toBe(0);
  expect(await page.evaluate(()=>(window as any).__exits)).toEqual([]);
  await page.evaluate(() => { (window as any).__failSave=false; });
  await page.getByRole('button',{name:'Retry save'}).click();
  await expect(page.getByRole('button',{name:'Retry save'})).toBeHidden();
  await exportButton.click();
  await expect.poll(()=>page.evaluate(()=>(window as any).__exports)).toBe(1);
  // Processing settings are locked while the export runs, so the DNG and the saved recipe cannot diverge.
  await expect(page.getByRole('slider',{name:'Blend with denoised'})).toBeDisabled();
  // An unrelated save can still fail; retain it before exiting.
  await page.evaluate(() => { (window as any).__failMeta=true; });
  await page.getByRole('button',{name:'Rate 3 stars'}).click();
  await expect(page.getByRole('button',{name:'Retry save'})).toBeVisible();
  await page.evaluate(()=>(window as any).__finishExport());
  await expect(page.getByRole('button',{name:'Retry save'})).toBeVisible();
  expect(await page.evaluate(()=>(window as any).__exits)).toEqual([]);
  await page.evaluate(() => { (window as any).__failMeta=false; });
  await page.getByRole('button',{name:'Retry save'}).click();
  await expect.poll(()=>page.evaluate(()=>(window as any).__exits)).toEqual([0]);
  expect(await page.evaluate(()=>(window as any).__exports)).toBe(1);
  // The exported settings and the persisted recipe agree.
  expect(await page.evaluate(()=>{
    const saves=(window as any).__recipeSaves ?? [];
    const exported=(window as any).__exportedRecipe;
    const saved=saves.at(-1);
    return !!saved && !!exported
      && JSON.stringify(saved.recipe.layers.map((l:any)=>l.blend))
         === JSON.stringify(exported.layers.map((l:any)=>l.blend));
  })).toBe(true);
});
