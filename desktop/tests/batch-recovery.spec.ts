import {test,expect} from '@playwright/test';

for (const damaged of [false, true]) test(`recovery survives Close and supports explicit discard (damaged=${damaged})`, async ({page}) => {
  await page.addInitScript((damaged) => {
    const queue = {snapshot:'saved',jobs:[{source:'/photos/a.ARW',output:'/out/a.dng',state:4,message:'Cancelled',completed:false}],completed:0,remaining:1};
    const calls:string[]=[];
    Object.assign(window,{isTauri:true,batchCalls:calls,__TAURI_INTERNALS__:{invoke:async(command:string,args:any)=>{
      if(command==='external_session'||command==='supersede_previews') return null;
      if(command==='processing_models') return {models:{denoise:[],sharpen:[],provider:'cpu'}};
      if(command==='desktop_control') {
        const op=args.request.operation; calls.push(op);
        if(op==='library_list') return {paths:[]};
        if(op==='preset_list') return {names:[]};
        if(op==='batch_status' && damaged) throw Error('Damaged snapshot');
        if(op==='batch_status'||op==='batch_retry'||op==='batch_run') return queue;
        if(op==='batch_discard') return {snapshot:null,jobs:[],completed:0,remaining:0};
      }
      return {};
    }}});
  }, damaged);
  await page.goto('/');
  const dialog=page.getByRole('dialog',{name:'Batch export'});
  if(damaged) await page.getByRole('button',{name:'Saved batch'}).click();
  await expect(dialog).toBeVisible();
  if(damaged) await expect(dialog.getByRole('alert')).toContainText('Damaged snapshot');
  else {
    await dialog.getByRole('button',{name:'Retry unfinished'}).click();
    await expect.poll(()=>page.evaluate(()=>(window as any).batchCalls.includes('batch_run'))).toBe(true);
  }
  await dialog.getByRole('button',{name:'Close',exact:true}).click();
  await expect(dialog).toHaveCount(0);
  expect(await page.evaluate(()=>(window as any).batchCalls.includes('batch_discard'))).toBe(false);
  await page.getByRole('button',{name:'Saved batch'}).click();
  await dialog.getByRole('button',{name:'Discard recovery'}).click();
  await expect(dialog).toHaveCount(0);
  expect(await page.evaluate(()=>(window as any).batchCalls.includes('batch_discard'))).toBe(true);
});

for (const failed of [false, true]) test(`batch waits for restoration and rejects unreadable recipes (failed=${failed})`, async ({page}) => {
  await page.addInitScript((failed) => {
    const paths=['/photos/a.ARW','/photos/b.ARW'];
    let resolveSecond!: (value:unknown)=>void;
    let released = false;
    const recipes:any[]=[];
    const queue={snapshot:'new',jobs:paths.map(source=>({source,output:source+'.dng',state:0,message:'',completed:false})),completed:0,remaining:2};
    Object.assign(window,{isTauri:true,batchRecipes:recipes,resolveSecond:()=>{released=true;resolveSecond(failed
      ? {outcome:'invalid',recipe:null,error:'Unreadable recipe'}
      : {outcome:'loaded',recipe:{layers:[{id:0,blend:82}]}});},
      __TAURI_INTERNALS__:{invoke:async(command:string,args:any)=>{
        if(command==='external_session'||command==='supersede_previews') return null;
        if(command==='plugin:dialog|open') return '/out';
        if(command==='processing_models') return {models:{denoise:[],sharpen:[],provider:'cpu'}};
        if(command==='render_raw_preview') return {version:1,width:10,height:10,png:'',metadata:{}};
        if(command==='desktop_control') {
          const op=args.request.operation;
          if(op==='library_list') return {paths};
          if(op==='preset_list') return {names:[]};
          if(op==='batch_status') return {snapshot:null,jobs:[],completed:0,remaining:0};
          if(op==='library_metadata') return {rating:0,labels:[]};
          if(op==='recipe_load') {
            if(args.request.path===paths[1]) return released
              ? {outcome:'invalid',recipe:null,error:'Unreadable recipe'}
              : new Promise(resolve=>{resolveSecond=resolve;});
            return {outcome:'loaded',recipe:{layers:[{id:0,blend:21}]}};
          }
          if(op==='batch_create') {recipes.push(...args.request.recipes);return queue;}
          if(op==='batch_run') return queue;
        }
        return {};
      }}});
  }, failed);
  await page.goto('/');
  await page.getByRole('button',{name:'Select a.ARW',exact:true}).click();
  await page.getByRole('button',{name:'Select b.ARW',exact:true}).click({modifiers:['Control']});
  await page.getByRole('button',{name:'Edit',exact:true}).click();
  await page.getByRole('button',{name:'Export selected (2)',exact:true}).click();
  expect(await page.evaluate(()=>(window as any).batchRecipes.length)).toBe(0);
  await page.evaluate(()=>(window as any).resolveSecond());
  if(failed) {
    await expect(page.getByText('Batch export failed: Error: Could not restore recipe for b.ARW')).toBeVisible();
    expect(await page.evaluate(()=>(window as any).batchRecipes.length)).toBe(0);
  } else {
    await expect.poll(()=>page.evaluate(()=>(window as any).batchRecipes.map((r:any)=>r.layers[0].blend))).toEqual([21,82]);
  }
});

test('batch controls are scoped to export instead of the Library workspace', async ({page}) => {
  await page.addInitScript(() => {
    Object.assign(window, {isTauri:true,__TAURI_INTERNALS__:{invoke:async(command:string,args:any)=>{
      if(command==='external_session'||command==='supersede_previews') return null;
      if(command === 'processing_models') return {models:{denoise:[],sharpen:[],provider:'cpu'}};
      if(command === 'desktop_control') {
        const operation = args.request.operation;
        if(operation === 'library_list') return {paths:[]};
        if(operation === 'preset_list') return {names:[]};
        if(operation === 'batch_status') return {snapshot:null,jobs:[],completed:0,remaining:0};
      }
      throw new Error(`Unexpected native command: ${command}`);
    }}});
  });
  await page.goto('/');
  await expect(page.getByRole('button',{name:'Library',exact:true})).toBeVisible();
  await expect(page.getByRole('button',{name:'Batch',exact:true})).toHaveCount(0);
  await expect(page.getByRole('region',{name:'Batch queue'})).toHaveCount(0);
  await expect(page.getByRole('button',{name:/Export selected/})).toHaveCount(0);
});
