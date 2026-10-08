import {test,expect} from '@playwright/test';

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
