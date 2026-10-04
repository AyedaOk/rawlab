import {expect, test} from '@playwright/test';

test('Update installs missing Okeinla before processing and retries failed downloads', async ({page}) => {
  await page.addInitScript(() => {
    let installed = false;
    let attempts = 0;
    const calls: string[] = [];
    const png = 'iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+jRZkAAAAASUVORK5CYII=';
    const preview = {version:1,width:100,height:80,png,quality:'high',metadata:{rawWidth:100,rawHeight:80}};
    Object.assign(window, {isTauri:true, __modelCalls:calls, __TAURI_INTERNALS__:{invoke:async (command:string,args:any) => {
      if(command === 'external_session') return null;
      if(command === 'processing_models') return {models:{denoise:installed ? [{id:'hf:rawdenoise-okeinla',name:'Okeinla',available:true}] : [],sharpen:[],provider:'cpu'}};
      if(command === 'render_raw_preview') return preview;
      if(command === 'raw_source_version') return 'v1';
      if(command === 'process_raw') {
        if(!installed) throw new Error('Processing started before download');
        calls.push('process');
        return {...preview,resultId:'result',before:png};
      }
      if(command === 'desktop_control') {
        const request = args.request;
        if(request.operation === 'library_list') return {paths:['/photos/test.ARW']};
        if(request.operation === 'preset_list') return {names:[]};
        if(request.operation === 'recipe_load') return {outcome:'missing',recipe:null};
        if(request.operation === 'library_metadata') return {rating:0,labels:[]};
        if(request.operation === 'model_download') {
          if(request.id !== 'rawdenoise-okeinla') throw new Error('Wrong model');
          calls.push('download');
          if(++attempts === 1) throw new Error('Test download failed');
          installed = true;
          return {catalog:[]};
        }
        return {saved:true};
      }
      return null;
    }}});
  });
  await page.goto('/');
  await page.getByRole('button',{name:'Select test.ARW',exact:true}).dblclick();
  await page.getByRole('button',{name:'Update',exact:true}).click();
  await expect.poll(() => page.evaluate(() => (window as any).__modelCalls)).toEqual(['download']);
  await expect(page.getByRole('button',{name:'Update',exact:true})).toBeEnabled();
  await page.getByRole('button',{name:'Update',exact:true}).click();
  await expect.poll(() => page.evaluate(() => (window as any).__modelCalls)).toEqual(['download','download','process']);
});
