import { expect, test } from '@playwright/test';

test('restoration is per-photo and switching flushes the previous recipe', async ({page}) => {
  await page.addInitScript(() => {
    const png = 'iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+jRZkAAAAASUVORK5CYII=';
    const recipe = (model: string, blend: number) => ({sharpenModelId:model,highlights:true,layers:[{id:0,name:'Global',enabled:true,modelId:'denoise',blend,sharpen:'AI',strength:30,marks:[],points:[]}]});
    const recipes: Record<string, unknown> = {'/photos/a.ARW':recipe('sharp-a',17),'/photos/b.ARW':recipe('sharp-b',29)};
    const saves: any[] = [];
    Object.assign(window, {isTauri:true,__recipeSaves:saves,__TAURI_INTERNALS__:{invoke:async(command:string,args:any)=>{
      if(command==='external_session') return null;
      if(command==='processing_models') return {models:{denoise:[{id:'denoise',name:'Denoise',available:true}],sharpen:[{id:'sharp-a',name:'Sharp A',available:true},{id:'sharp-b',name:'Sharp B',available:true}],provider:'cpu'}};
      if(command==='render_raw_preview') return {version:1,width:100,height:100,png,quality:'fast',metadata:{make:'Test',model:'Camera',lens:'',iso:100,aperture:2.8,exposureSeconds:.01,exposureBiasEv:0,focalLengthMm:50,rawWidth:100,rawHeight:100}};
      if(command==='desktop_control') {
        const request=args.request;
        if(request.operation==='library_list') return {paths:Object.keys(recipes)};
        if(request.operation==='preset_list') return {names:[]};
        if(request.operation==='recipe_load') return {recipe:recipes[request.path]};
        if(request.operation==='recipe_save') {saves.push(structuredClone(request));return {saved:true};}
        if(request.operation==='library_metadata'||request.operation==='library_metadata_save') return {rating:0,labels:[]};
      }
      throw new Error(`Unexpected native command: ${command}`);
    }}});
  });
  await page.goto('/');
  await expect(page.getByRole('button',{name:'Select a.ARW',exact:true})).toBeVisible();
  await page.getByRole('button',{name:'Select a.ARW',exact:true}).dblclick();
  await expect(page.getByRole('combobox',{name:'AI sharpening model'})).toHaveValue('sharp-a');
  await page.getByRole('slider',{name:'Blend with denoised'}).fill('63');
  await page.getByRole('button',{name:'Edit b.ARW',exact:true}).click();
  await expect(page.getByRole('combobox',{name:'AI sharpening model'})).toHaveValue('sharp-b');
  await expect.poll(() => page.evaluate(() => (window as any).__recipeSaves.filter((save:any)=>save.path==='/photos/a.ARW').length)).toBe(1);
  expect(await page.evaluate(() => (window as any).__recipeSaves.find((save:any)=>save.path==='/photos/a.ARW').recipe.layers[0].blend)).toBe(63);
  await page.getByRole('button',{name:'Edit a.ARW',exact:true}).click();
  await expect(page.getByRole('combobox',{name:'AI sharpening model'})).toHaveValue('sharp-a');
});

test('failed recipe save remains blocked until Retry save is used', async ({page}) => {
  await page.addInitScript(() => {
    const png='iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+jRZkAAAAASUVORK5CYII=';
    let attempts=0;
    Object.assign(window,{isTauri:true,__saveAttempts:()=>attempts,__TAURI_INTERNALS__:{invoke:async(command:string,args:any)=>{
      if(command==='external_session') return null;
      if(command==='processing_models') return {models:{denoise:[{id:'denoise',name:'Denoise',available:true}],sharpen:[],provider:'cpu'}};
      if(command==='render_raw_preview') return {version:1,width:100,height:100,png,quality:'fast',metadata:{rawWidth:100,rawHeight:100}};
      if(command==='desktop_control') {
        const request=args.request;
        if(request.operation==='library_list') return {paths:['/photos/a.ARW']};
        if(request.operation==='preset_list') return {names:[]};
        if(request.operation==='recipe_load') return {recipe:{layers:[{id:0,name:'Global',enabled:true,modelId:'denoise',blend:20,sharpen:'Off',strength:30,marks:[],points:[]}]}};
        if(request.operation==='recipe_save') {attempts++;if(attempts===1) throw new Error('disk full');return {saved:true};}
        if(request.operation==='library_metadata'||request.operation==='library_metadata_save') return {rating:0,labels:[]};
      }
      throw new Error(`Unexpected native command: ${command}`);
    }}});
  });
  await page.goto('/');
  await page.getByRole('button',{name:'Select a.ARW',exact:true}).dblclick();
  await page.getByRole('slider',{name:'Blend with denoised'}).fill('41');
  await expect(page.getByText(/Could not save recipe:.*disk full/)).toBeVisible();
  await page.getByRole('slider',{name:'Blend with denoised'}).fill('42');
  await page.waitForTimeout(900);
  expect(await page.evaluate(() => (window as any).__saveAttempts())).toBe(1);
  await page.getByRole('button',{name:'Retry save'}).click();
  await expect.poll(() => page.evaluate(() => (window as any).__saveAttempts())).toBe(2);
  await expect(page.getByRole('button',{name:'Retry save'})).toBeHidden();
});

test('persistence queue serializes first and latest revisions', async ({page}) => {
  await page.goto('/');
  const result = await page.evaluate(async () => {
    const {PersistenceQueue} = await import('/src/persistence.ts');
    const calls: string[] = [];
    let releaseA!: () => void;
    const a = new Promise<void>((resolve) => { releaseA = resolve; });
    const queue = new PersistenceQueue(() => {});
    queue.schedule('recipe:a', 0, async () => { calls.push('A'); await a; });
    await new Promise((resolve) => setTimeout(resolve, 20));
    queue.schedule('recipe:a', 0, async () => { calls.push('B'); });
    queue.schedule('recipe:a', 0, async () => { calls.push('C'); });
    const flushed = queue.flush('recipe:a');
    await new Promise((resolve) => setTimeout(resolve, 20));
    const beforeRelease = [...calls];
    releaseA();
    return {beforeRelease, calls: await flushed.then(() => calls), saved: await flushed};
  });
  expect(result.beforeRelease).toEqual(['A']);
  expect(result.calls).toEqual(['A', 'C']);
  expect(result.saved).toBe(true);
});

test('flushAll drains keys introduced while another save is running', async ({page}) => {
  await page.goto('/');
  const result = await page.evaluate(async () => {
    const {PersistenceQueue} = await import('/src/persistence.ts');
    const calls: string[] = [];
    const queue = new PersistenceQueue(() => {});
    queue.schedule('recipe:a', 0, async () => {
      calls.push('A');
      queue.schedule('metadata:b', 0, async () => { calls.push('B'); });
    });
    return {saved: await queue.flushAll(), calls};
  });
  expect(result).toEqual({saved: true, calls: ['A', 'B']});
});

test('an edit made during recipe restoration is preserved and saved', async ({page}) => {
  await page.addInitScript(() => {
    const png='iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+jRZkAAAAASUVORK5CYII=';
    let resolveLoad!: (value: unknown) => void;
    let loads=0;
    const saves: any[]=[];
    Object.assign(window,{isTauri:true,__recipeSaves:saves,__recipeLoads:()=>loads,
      __resolveRecipeLoad:()=>resolveLoad({recipe:{layers:[{id:0,name:'Global',enabled:true,modelId:'denoise',blend:12,sharpen:'Off',strength:30,marks:[],points:[]}]}}),
      __TAURI_INTERNALS__:{invoke:async(command:string,args:any)=>{
        if(command==='external_session') return null;
        if(command==='processing_models') return {models:{denoise:[{id:'denoise',name:'Denoise',available:true}],sharpen:[],provider:'cpu'}};
        if(command==='render_raw_preview') return {version:1,width:321,height:213,png,quality:'fast',metadata:{rawWidth:321,rawHeight:213}};
        if(command==='desktop_control') {
          const request=args.request;
          if(request.operation==='library_list') return {paths:['/photos/a.ARW']};
          if(request.operation==='preset_list') return {names:[]};
          if(request.operation==='recipe_load') {loads++;return new Promise((resolve)=>{resolveLoad=resolve;});}
          if(request.operation==='recipe_save') {saves.push(structuredClone(request));return {saved:true};}
          if(request.operation==='library_metadata'||request.operation==='library_metadata_save') return {rating:0,labels:[]};
        }
        throw new Error(`Unexpected native command: ${command}`);
      }}});
  });
  await page.goto('/');
  await expect(page.getByRole('button',{name:'Select a.ARW',exact:true})).toBeVisible();
  await page.getByRole('button',{name:'Select a.ARW',exact:true}).dblclick();
  await page.getByRole('slider',{name:'Blend with denoised'}).fill('67');
  await page.evaluate(() => (window as any).__resolveRecipeLoad());
  await expect(page.getByRole('slider',{name:'Blend with denoised'})).toHaveValue('67');
  await expect.poll(() => page.evaluate(() => (window as any).__recipeSaves.length)).toBe(1);
  const saved = await page.evaluate(() => (window as any).__recipeSaves[0]);
  expect(saved.recipe.layers[0].blend).toBe(67);
  expect([saved.width,saved.height]).toEqual([321,213]);
  expect(await page.evaluate(() => (window as any).__recipeLoads())).toBe(1);
});

test('saved recipe is restored after a page restart', async ({page}) => {
  const recipes = new Map<string, any>();
  const png='iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+jRZkAAAAASUVORK5CYII=';
  await page.exposeFunction('__rawlabNativeInvoke', async (command:string,args:any) => {
    if(command==='external_session') return null;
    if(command==='processing_models') return {models:{denoise:[{id:'denoise',name:'Denoise',available:true}],sharpen:[],provider:'cpu'}};
    if(command==='render_raw_preview') return {version:1,width:213,height:321,png,quality:'fast',metadata:{rawWidth:213,rawHeight:321}};
    if(command==='desktop_control') {
      const request=args.request;
      if(request.operation==='library_list') return {paths:['/photos/a.ARW']};
      if(request.operation==='preset_list') return {names:[]};
      if(request.operation==='recipe_load') return {outcome:recipes.has(request.path)?'loaded':'missing',recipe:recipes.get(request.path)??null};
      if(request.operation==='recipe_save') {recipes.set(request.path,structuredClone(request.recipe));return {saved:true};}
      if(request.operation==='library_metadata'||request.operation==='library_metadata_save') return {rating:0,labels:[]};
    }
    throw new Error(`Unexpected native command: ${command}`);
  });
  await page.addInitScript(() => Object.assign(window,{isTauri:true,__TAURI_INTERNALS__:{invoke:(command:string,args:any)=>(window as any).__rawlabNativeInvoke(command,args)}}));
  await page.goto('/');
  await page.getByRole('button',{name:'Select a.ARW',exact:true}).dblclick();
  await page.getByRole('slider',{name:'Blend with denoised'}).fill('58');
  await expect.poll(() => recipes.get('/photos/a.ARW')?.layers[0].blend).toBe(58);
  await page.reload();
  await page.getByRole('button',{name:'Select a.ARW',exact:true}).dblclick();
  await expect(page.getByRole('slider',{name:'Blend with denoised'})).toHaveValue('58');
});

test('close waits for saves and remains open after a failed write', async ({page}) => {
  await page.addInitScript(() => {
    const png='iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+jRZkAAAAASUVORK5CYII=';
    const callbacks=new Map<number,(event:any)=>unknown>();
    let callbackId=0;
    let closeHandler:(event:any)=>unknown=()=>{};
    let releaseSave!:(fail:boolean)=>void;
    let destroyed=0;
    let saveAttempts=0;
    const saveBarrier=new Promise<boolean>((resolve)=>{releaseSave=resolve;});
    Object.assign(window,{isTauri:true,__releaseSave:(fail:boolean)=>releaseSave(fail),__destroyed:()=>destroyed,
      __requestClose:()=>closeHandler({event:'tauri://close-requested',id:1,payload:null}),
      __TAURI_INTERNALS__:{metadata:{currentWindow:{label:'main'}},
        transformCallback:(callback:(event:any)=>unknown)=>{const id=++callbackId;callbacks.set(id,callback);return id;},
        invoke:async(command:string,args:any)=>{
          if(command==='external_session') return null;
          if(command==='plugin:event|listen') {
            if(args.event==='tauri://close-requested') closeHandler=callbacks.get(args.handler)!;
            return callbackId;
          }
          if(command==='plugin:event|unlisten') return null;
          if(command==='plugin:window|destroy') {destroyed++;return null;}
          if(command==='processing_models') return {models:{denoise:[{id:'denoise',name:'Denoise',available:true}],sharpen:[],provider:'cpu'}};
          if(command==='render_raw_preview') return {version:1,width:100,height:80,png,quality:'fast',metadata:{rawWidth:100,rawHeight:80}};
          if(command==='desktop_control') {
            const request=args.request;
            if(request.operation==='library_list') return {paths:['/photos/a.ARW']};
            if(request.operation==='preset_list') return {names:[]};
            if(request.operation==='recipe_load') return {outcome:'missing',recipe:null};
            if(request.operation==='recipe_save') {
              saveAttempts++;
              if(saveAttempts===1 && await saveBarrier) throw new Error('disk full');
              return {saved:true};
            }
            if(request.operation==='library_metadata'||request.operation==='library_metadata_save') return {rating:0,labels:[]};
          }
          throw new Error(`Unexpected native command: ${command}`);
        }}});
  });
  await page.goto('/');
  await page.getByRole('button',{name:'Select a.ARW',exact:true}).dblclick();
  await page.getByRole('slider',{name:'Blend with denoised'}).fill('73');
  const close = page.evaluate(() => (window as any).__requestClose());
  await page.waitForTimeout(200);
  expect(await page.evaluate(() => (window as any).__destroyed())).toBe(0);
  await page.evaluate(() => (window as any).__releaseSave(true));
  await close;
  expect(await page.evaluate(() => (window as any).__destroyed())).toBe(0);
  await expect(page.getByRole('button',{name:'Retry save'})).toBeVisible();
  await page.getByRole('button',{name:'Retry save'}).click();
  await expect(page.getByRole('button',{name:'Retry save'})).toBeHidden();
  await page.evaluate(() => (window as any).__requestClose());
  await expect.poll(() => page.evaluate(() => (window as any).__destroyed())).toBe(1);
});

test('an invalid stored recipe is never overwritten by automatic saving', async ({page}) => {
  await page.addInitScript(() => {
    const png='iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+jRZkAAAAASUVORK5CYII=';
    let saves=0, repaired=false;
    Object.assign(window,{isTauri:true,__repairRecipe:()=>{repaired=true;},__recipeSaves:()=>saves,__TAURI_INTERNALS__:{invoke:async(command:string,args:any)=>{
      if(command==='external_session') return null;
      if(command==='processing_models') return {models:{denoise:[{id:'denoise',name:'Denoise',available:true}],sharpen:[],provider:'cpu'}};
      if(command==='render_raw_preview') return {version:1,width:100,height:80,png,quality:'fast',metadata:{rawWidth:100,rawHeight:80}};
      if(command==='desktop_control') {
        const request=args.request;
        if(request.operation==='library_list') return {paths:['/photos/a.ARW']};
        if(request.operation==='preset_list') return {names:[]};
        if(request.operation==='recipe_load') return repaired
          ? {outcome:'missing',recipe:null} : {outcome:'invalid',error:'Malformed recipe JSON',recipe:null};
        if(request.operation==='recipe_save') {saves++;return {saved:true};}
        if(request.operation==='library_metadata'||request.operation==='library_metadata_save') return {rating:0,labels:[]};
      }
      throw new Error(`Unexpected native command: ${command}`);
    }}});
  });
  await page.goto('/');
  await expect(page.getByText(/Could not restore recipe for a.ARW:.*Malformed recipe JSON/)).toBeVisible();
  await page.getByRole('button',{name:'Select a.ARW',exact:true}).dblclick();
  await page.getByRole('slider',{name:'Blend with denoised'}).fill('44');
  await page.waitForTimeout(900);
  expect(await page.evaluate(() => (window as any).__recipeSaves())).toBe(0);
  await page.evaluate(() => (window as any).__repairRecipe());
  await page.getByRole('button', {name:'Retry restore'}).click();
  await expect(page.getByText(/Could not restore recipe for/)).toHaveCount(0);
  await expect(page.getByRole('button', {name:'Retry restore'})).toHaveCount(0);
});
