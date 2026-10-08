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
  await expect(page.locator('.photo-card')).toHaveCount(1);
  await page.getByRole('button', {name: 'Import Pictures', exact:true}).click();
  await expect(page.getByRole('button', {name: 'Select native.png'})).toBeVisible();
  await expect(page.getByText('1 unsupported, unreadable or oversized entries skipped', {exact:false})).toBeVisible();
});

test('RAW thumbnails, metadata and quality changes use the native renderer', async ({ page }) => {
  await page.addInitScript(() => {
    const png = 'iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+jRZkAAAAASUVORK5CYII=';
    const calls:any[]=[];
    Object.assign(window, { isTauri: true, __processingCalls:calls, __TAURI_INTERNALS__: {
      invoke: async (command: string, args: Record<string, any>) => {
        if(command === 'process_raw' || command === 'render_processed') calls.push({command,...structuredClone(args)});
        if (command === 'external_session' || command === 'supersede_previews') return null;
        if(command === 'plugin:dialog|open') return ['/photos/camera.ARW'];
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

test('external edit exports to the reserved path and returns success', async ({page}) => {
  await page.addInitScript(() => {
    const png='iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+jRZkAAAAASUVORK5CYII=';
    const calls:any[]=[];
    Object.assign(window,{isTauri:true,__externalCalls:calls,__TAURI_INTERNALS__:{invoke:async(command:string,args:any)=>{
      calls.push({command,args:structuredClone(args)});
      if(command==='external_session') return {mode:'single',sources:['/photos/a.ARW'],output:'/photos/a-rawlab.dng'};
      if(command==='supersede_previews') return null;
      if(command==='processing_models') return {models:{denoise:[{id:'denoise',name:'Denoise',available:true}],sharpen:[],provider:'cpu'}};
      if(command==='render_raw_preview') return {version:1,width:100,height:80,sourceWidth:100,sourceHeight:80,png,quality:'fast',metadata:{rawWidth:100,rawHeight:80}};
      if(command==='process_raw') return {version:1,width:100,height:80,png,before:png,output:args.output,quality:'fast',metadata:{}};
      if(command==='finish_external') return null;
      if(command==='desktop_control') {
        const request=args.request;
        if(request.operation==='library_list') return {paths:[]};
        if(request.operation==='preset_list') return {names:[]};
        if(request.operation==='recipe_load') return {outcome:'missing',recipe:null};
        if(request.operation==='recipe_save') return {saved:true};
        if(request.operation==='library_metadata'||request.operation==='library_metadata_save') return {rating:0,labels:[]};
      }
      throw new Error(`Unexpected native command: ${command}`);
    }}});
  });
  await page.goto('/');
  await expect(page.getByRole('button',{name:'Export and return to Darktable'})).toBeVisible();
  await page.getByRole('button',{name:'Export and return to Darktable'}).click();
  await expect.poll(() => page.evaluate(() => (window as any).__externalCalls.some((call:any)=>call.command==='finish_external'&&call.args.code===0))).toBe(true);
  expect(await page.evaluate(() => {
    const call=(window as any).__externalCalls.find((item:any)=>item.command==='process_raw');
    return call.args.output==='/photos/a-rawlab.dng';
  })).toBe(true);
});
