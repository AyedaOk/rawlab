import { test, expect } from '@playwright/test';

test('CUDA and settings errors have dismiss buttons', async ({page}) => {
  await page.addInitScript(() => {
    const models = {denoise:[],sharpen:[],provider:'cpu'};
    Object.assign(window, {isTauri:true,__TAURI_INTERNALS__:{invoke:async (command:string,args:any) => {
      if(command === 'external_session' || command === 'supersede_previews') return null;
      if(command === 'processing_models') return {models};
      if(command === 'desktop_control') {
        switch(args.request.operation) {
          case 'library_list': return {paths:[]};
          case 'preset_list': return {names:[]};
          case 'settings': return {...models,sam:[],activeSamModelId:'',runtimeLibrary:'',deviceId:0,
            providers:[],runtimeError:'Could not enable CUDA'};
          case 'settings_save': throw new Error('Could not save processing settings');
        }
      }
      return null;
    }}});
  });
  await page.goto('/');
  await page.getByRole('button', {name:'Preferences',exact:true}).click();
  await expect(page.getByText('Could not enable CUDA')).toBeVisible();
  await page.getByRole('button', {name:'Dismiss runtime error'}).click();
  await expect(page.getByText('Could not enable CUDA')).toHaveCount(0);
  await page.getByRole('button', {name:'Save processing settings'}).click();
  await expect(page.getByRole('alert')).toContainText('Could not save processing settings');
  await page.getByRole('button', {name:'Dismiss settings error'}).click();
  await expect(page.getByRole('alert')).toHaveCount(0);
});

test('catalog refresh exposes an already downloaded depth model for selection', async ({page}) => {
  await page.addInitScript(() => {
    let discovered=false, activeDepthModelId='';
    const models=() => ({denoise:[],sharpen:[],provider:'cpu',activeDepthModelId,
      depth:discovered ? [{id:'small',name:'Depth Anything V2 Small',path:'/models/model.onnx',origin:'verified',available:true}] : []});
    Object.assign(window,{isTauri:true,__TAURI_INTERNALS__:{invoke:async (command:string,args:any) => {
      if(command === 'processing_models') return {models:models()};
      if(command === 'desktop_control') {
        switch(args.request.operation) {
          case 'library_list': return {paths:[]};
          case 'preset_list': return {names:[]};
          case 'settings': return {...models(),sam:[],activeSamModelId:'',runtimeLibrary:'',deviceId:0,providers:[],runtimeError:''};
          case 'catalog':
            discovered=true;
            return {catalog:[{id:'small',name:'Depth Anything V2 Small',task:'depth',installed:true}]};
          case 'settings_save': activeDepthModelId=args.request.activeDepthModelId; return {saved:true};
        }
      }
      return null;
    }}});
  });
  await page.goto('/');
  await page.getByRole('button',{name:'Preferences',exact:true}).click();
  const selector=page.getByRole('combobox',{name:'Active depth model'});
  const configured=page.getByRole('group',{name:'Configured models'});
  await expect(selector.locator('option')).toHaveCount(1);
  await page.getByRole('button',{name:'Refresh catalog'}).click();
  await expect(configured.getByText('Depth Anything V2 Small',{exact:true})).toBeVisible();
  await expect(configured.getByRole('button',{name:'Remove',exact:true})).toBeVisible();
  await selector.selectOption('small');
  await page.getByRole('button',{name:'Save processing settings'}).click();
  await expect(selector).toBeEnabled();
  await expect(selector).toHaveValue('small');
  await selector.selectOption('');
  await page.getByRole('button',{name:'Save processing settings'}).click();
  await expect(selector).toBeEnabled();
  await expect(selector).toHaveValue('');
});

test('local depth models offer removal from the list', async ({page}) => {
  await page.addInitScript(() => {
    let removed = false;
    const models = () => ({denoise:[],sharpen:[],provider:'cpu',activeDepthModelId:removed?'':'local:depth',
      depth:removed?[]:[{id:'local:depth',name:'Local depth',path:'/models/depth.onnx',origin:'manual',available:true}]});
    Object.assign(window,{isTauri:true,__modelRemovals:[],__TAURI_INTERNALS__:{invoke:async(command:string,args:any)=>{
      if(command==='processing_models') return {models:models()};
      if(command==='desktop_control') {
        switch(args.request.operation) {
          case 'library_list': return {paths:[]};
          case 'preset_list': return {names:[]};
          case 'settings': return {...models(),sam:[],activeSamModelId:'',runtimeLibrary:'',deviceId:0,providers:[],runtimeError:''};
          case 'model_remove': (window as any).__modelRemovals.push(args.request); removed=true; return {saved:true};
        }
      }
      return null;
    }}});
  });
  await page.goto('/');
  await page.getByRole('button',{name:'Preferences',exact:true}).click();
  const configured = page.getByRole('group',{name:'Configured models'});
  await expect(configured.getByRole('button',{name:'Forget',exact:true})).toHaveCount(0);
  await configured.getByRole('button',{name:'Remove from list',exact:true}).click();
  await expect(configured.getByText('Local depth',{exact:true})).toHaveCount(0);
  await expect(page.getByRole('combobox',{name:'Active depth model'})).toHaveValue('');
  expect(await page.evaluate(() => (window as any).__modelRemovals)).toEqual([{operation:'model_remove',task:'depth',id:'local:depth'}]);
});

test('editor renders, recipe changes and preset naming dialog works', async ({ page }) => {
  const errors: string[] = [];
  page.on('pageerror', error => errors.push(error.message));
  await page.goto('/');
  await expect(page.getByRole('heading', { name: 'Library', exact: true })).toBeVisible();
  await expect(page.getByRole('complementary', {name: 'Library imports'})).toBeVisible();
  await expect(page.getByRole('button', {name: 'Import Pictures', exact: true})).toBeVisible();
  await expect(page.getByRole('button', {name: 'Import Folder', exact: true})).toBeVisible();
  await expect(page.getByRole('button', {name: 'Open in Edit'})).toHaveCount(0);
  await expect(page.getByRole('button', { name: 'Fit image' })).toHaveCount(0);
  await expect(page.getByRole('button', { name: 'Export DNG' })).toHaveCount(0);
  if (process.env.PROTOTYPE_SCREENSHOT) await page.screenshot({path: process.env.PROTOTYPE_SCREENSHOT + '-library.png'});
  await page.locator('.photo-card').first().dblclick();
  await expect(page.locator('.main-photo')).toBeVisible();
  await expect(page.getByRole('switch', {name:'High Quality'})).toHaveAttribute('aria-checked', 'false');
  if (process.env.PROTOTYPE_SCREENSHOT) await page.screenshot({path: process.env.PROTOTYPE_SCREENSHOT});
  await expect(page.getByRole('button', {name: 'Edit Mask', exact: true})).toBeDisabled();
  await expect(page.getByRole('button', {name: 'Delete selected mask'})).toBeDisabled();
  await expect(page.getByRole('button', { name: 'Normal', exact: true })).toBeEnabled();
  await expect(page.getByRole('group', {name:'Sharpening mode'}).getByRole('button', {name:'AI',exact:true})).toBeDisabled();
  await expect(page.getByRole('group', { name: 'Chromatic aberration' }).getByRole('button', { name: 'On' })).toBeDisabled();
  await expect(page.getByText(/mock/i)).toHaveCount(0);
  await page.getByRole('button', { name: 'Add mask', exact: true }).click();
  await expect(page.getByRole('button', { name: 'Disable Mask 1' })).toBeVisible();
  await page.getByRole('button', { name: 'Linear RGB' }).click();
  await expect(page.getByRole('group', { name: 'Chromatic aberration' }).getByRole('button', { name: 'On' })).toBeEnabled();
  await page.getByRole('button', { name: 'Normal', exact: true }).click();
  await expect(page.getByRole('slider', { name: 'Strength' })).toBeEnabled();
  await expect(page.locator('.layer-actions').getByRole('button', {name: 'Edit Mask', exact: true})).toBeEnabled();
  const highlights = page.getByRole('group', {name: 'Preserve highlights'});
  await expect(highlights.getByRole('button', {name: 'On', exact: true})).toHaveAttribute('aria-pressed', 'true');
  await highlights.getByRole('button', {name: 'Off', exact: true}).click();
  await page.getByRole('button', {name:'Presets', exact:true}).click();
  await expect(page.getByRole('button', {name:'Delete selected preset'})).toBeDisabled();
  await page.getByRole('button', {name:'Save current settings…'}).click();
  await expect(page.getByRole('dialog', {name: 'Save preset'})).toBeVisible();
  await expect(page.getByRole('dialog').getByRole('button', {name: 'Save', exact: true})).toBeDisabled();
  await page.getByRole('textbox', { name: 'Preset name' }).fill('Landscape');
  await page.getByRole('dialog').getByRole('button', { name: 'Save', exact: true }).click();
  await page.getByRole('button', {name:'Presets', exact:true}).click();
  await expect(page.getByRole('button', {name:'Landscape', exact:true})).toHaveAttribute('aria-pressed', 'true');
  await page.getByRole('button', {name:'Save current settings…'}).click();
  await expect(page.getByRole('textbox', {name:'Preset name'})).toHaveValue('Landscape');
  await page.getByRole('button', {name:'Cancel', exact:true}).click();
  await page.getByRole('button', { name: 'Delete selected mask' }).click();
  await expect(page.getByRole('combobox', {name:'Preset', exact:true})).toHaveCount(0);
  await page.getByRole('button', {name:'Presets', exact:true}).click();
  await page.getByRole('button', {name:'Landscape', exact:true}).click();
  await expect(page.getByRole('button', { name: 'Disable Mask 1' })).toBeVisible();
  await page.getByRole('button', { name: 'Update', exact: true }).click();
  await expect(page.getByText('No denoise or sharpening was performed.', { exact: false })).toBeVisible();
  await expect(page.getByRole('button', { name: 'Update', exact: true })).toHaveCount(0);
  await page.getByRole('button', {name:'Presets', exact:true}).click();
  await page.getByRole('button', {name:'Delete selected preset'}).click();
  await expect(page.getByRole('button', {name:'Landscape', exact:true})).toHaveCount(0);
  await expect(page.getByRole('button', {name:'Disable Mask 1'})).toBeVisible();
  await page.keyboard.press('Escape');
  await expect(page.getByRole('region', {name:'Saved presets'})).toHaveCount(0);
  expect(errors).toEqual([]);
});

test('mask editing is contextual and Done preserves changes', async ({page}) => {
  await page.goto('/');
  await page.locator('.photo-card').first().dblclick();
  await expect(page.getByRole('region', {name:'Mask editing'})).toHaveCount(0);
  await expect(page.getByRole('slider', {name:'Feather'})).toHaveCount(0);
  await page.getByRole('button', {name:'Add mask', exact:true}).click();
  await page.getByRole('button', {name:'Edit Mask', exact:true}).click();
  await expect(page.getByRole('region', {name:'Mask editing'})).toBeVisible();
  await page.getByRole('button', {name:'Brush', exact:true}).click();
  await expect(page.getByRole('button', {name:'Brush', exact:true})).toHaveAttribute('aria-pressed','true');
  const photoBounds = await page.locator('.photo-frame').boundingBox();
  if(!photoBounds) throw new Error('Missing mask drawing surface');
  await page.mouse.move(photoBounds.x + photoBounds.width*.4, photoBounds.y + photoBounds.height*.4);
  await page.mouse.down();
  await page.mouse.move(photoBounds.x + photoBounds.width*.5, photoBounds.y + photoBounds.height*.5, {steps:5});
  await page.mouse.up();
  await expect(page.locator('.mask-overlay')).toHaveAttribute('data-mark-count', '1');
  await page.keyboard.press('Control+z');
  await expect(page.getByRole('region', {name:'Mask editing'})).toBeVisible();
  await expect(page.locator('.mask-overlay')).toHaveAttribute('data-mark-count', '0');
  await page.keyboard.press('Control+Shift+z');
  await expect(page.locator('.mask-overlay')).toHaveAttribute('data-mark-count', '1');
  await page.getByRole('slider', {name:'Feather', exact:true}).fill('42');
  await page.getByRole('switch', {name:'Invert mask', exact:true}).click();
  await page.getByRole('button', {name:'Done', exact:true}).click();
  await expect(page.getByRole('slider', {name:'Feather'})).toHaveCount(0);
  await page.getByRole('button', {name:'Edit Mask', exact:true}).click();
  await expect(page.getByRole('slider', {name:'Feather'})).toHaveValue('42');
  await expect(page.getByRole('switch', {name:'Invert mask', exact:true})).toHaveAttribute('aria-checked', 'true');
  await page.getByRole('button', {name:'Add mask', exact:true}).click();
  await expect(page.getByRole('region', {name:'Mask editing'})).toBeVisible();
  await expect(page.getByRole('slider', {name:'Feather'})).toHaveValue('0');
  await page.getByRole('button', {name:'Delete selected mask'}).click();
  await expect(page.getByRole('region', {name:'Mask editing'})).toHaveCount(0);
});

test('comparison, export explanation and narrow layout', async ({ page }) => {
  await page.goto('/');
  await page.locator('.photo-card').first().dblclick();
  await expect(page.getByRole('complementary')).toHaveCount(1);
  await expect(page.locator('.topbar').getByRole('button', {name: 'Export DNG'})).toHaveCount(0);
  await page.getByRole('button', { name: 'Before and after' }).click();
  await page.getByRole('button', {name:'Zoom in',exact:true}).click();
  await page.getByRole('button', {name:'1:1', exact:true}).click();
  const enlarged = await page.locator('.main-photo').boundingBox();
  if(!enlarged) throw new Error('Missing image');
  const startX = enlarged.x + enlarged.width * .3, startY = enlarged.y + enlarged.height * .4;
  await page.mouse.move(startX, startY); await page.mouse.down();
  await page.mouse.move(startX + 40, startY + 30, {steps:5}); await page.mouse.up();
  await expect(page.locator('.photo-frame')).not.toHaveCSS('transform', 'matrix(1.25, 0, 0, 1.25, 0, 0)');
  await page.getByRole('button', {name:'Fit image',exact:true}).click();
  await expect(page.locator('.photo-frame')).toHaveCSS('transform', 'matrix(1, 0, 0, 1, 0, 0)');
  const divider = page.getByRole('separator', { name: 'Before after divider' });
  await expect(divider).toBeVisible();
  await expect(page.getByRole('slider', { name: 'Before after divider' })).toHaveCount(0);
  const bounds = await page.locator('.photo-frame').boundingBox();
  const handle = await divider.boundingBox();
  if (!bounds || !handle) throw new Error('Missing comparison geometry');
  await page.mouse.move(handle.x + handle.width / 2, handle.y + handle.height / 2);
  await page.mouse.down();
  await page.mouse.move(bounds.x + bounds.width * .75, handle.y + handle.height / 2, {steps: 8});
  await page.mouse.up();
  await expect(divider).toHaveAttribute('aria-valuenow', '75');
  await divider.focus();
  await page.keyboard.press('ArrowLeft');
  await expect(divider).toHaveAttribute('aria-valuenow', '73');
  await page.getByRole('button', { name: 'Export DNG', exact: true }).first().click();
  await expect(page.getByRole('dialog')).toBeVisible();
  await page.getByRole('button', { name: 'Close' }).click();
  await page.getByRole('button', { name: 'Preferences', exact: true }).click();
  await page.keyboard.press('Escape');
  await expect(page.getByRole('dialog')).toHaveCount(0);
  await page.setViewportSize({ width: 1000, height: 700 });
  expect(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth)).toBe(true);
  await page.setViewportSize({ width: 390, height: 844 });
  expect(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth)).toBe(true);
});

test('local preview file stays local and library opens', async ({ page }) => {
  await page.goto('/');
  await page.getByLabel('Import picture files').setInputFiles({name: 'test.png', mimeType: 'image/png', buffer: Buffer.from('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+jRZkAAAAASUVORK5CYII=', 'base64')});
  await expect(page.getByRole('button', {name: 'Select test.png', exact: true})).toBeVisible();
  await expect(page.getByRole('button', {name: /Alpine lake/})).toHaveCount(0);
  await page.getByRole('button', {name: 'Select test.png', exact:true}).focus();
  await page.keyboard.press('Enter');
  await expect(page.locator('.main-photo')).toHaveAttribute('src', /^blob:/);
  await page.getByRole('button', { name: 'Library', exact: true }).click();
  await expect(page.getByRole('heading', { name: 'Library', exact: true })).toBeVisible();
});

test('folder import supports multiple files and filtering', async ({ page }, testInfo) => {
  const { mkdir, writeFile } = await import('node:fs/promises');
  const folder = testInfo.outputPath('photos');
  await mkdir(folder, {recursive:true});
  const png = Buffer.from('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+jRZkAAAAASUVORK5CYII=', 'base64');
  await writeFile(folder + '/one.png', png);
  await writeFile(folder + '/two.png', png);
  await writeFile(folder + '/three.png', png);
  await writeFile(folder + '/unsupported.ARW', 'not a preview');
  await page.goto('/');
  await page.getByLabel('Import folder files').setInputFiles(folder);
  await expect(page.locator('.photo-card')).toHaveCount(3);
  await page.locator('.photo-card').nth(0).click();
  await page.locator('.photo-card').nth(2).click({modifiers:['Shift']});
  await expect(page.locator('.selection-check')).toHaveCount(3);
  await page.locator('.photo-card').nth(1).click();
  await expect(page.locator('.selection-check')).toHaveCount(1);
  await expect(page.locator('.photo-card').nth(1).locator('.selection-check')).toBeVisible();
  await page.locator('.photo-card').nth(0).click();
  await page.locator('.photo-card').nth(2).click({modifiers:['Shift']});
  await page.keyboard.press('Enter');
  await expect(page.locator('.filmstrip-selection-check')).toHaveCount(3);
  await page.getByRole('button', {name:'Back to library'}).click();
  await expect(page.getByText('Rate selected 5', {exact:true})).toHaveCount(0);
  await expect(page.getByText('Imported 3 previews', {exact:false})).toBeVisible();
  await expect(page.getByRole('textbox', {name:'Filter filename'})).toHaveCount(0);
  await page.getByRole('button', {name:'Filter photographs'}).click();
  await page.getByRole('textbox', {name:'Filter filename'}).fill('two.png');
  await expect(page.locator('.photo-card')).toHaveCount(1);
  await page.getByRole('textbox', {name:'Filter filename'}).clear();
  await page.getByRole('combobox', {name:'File type'}).selectOption('png');
  await expect(page.locator('.photo-card')).toHaveCount(3);
  await page.getByRole('combobox', {name:'Minimum rating'}).selectOption('3');
  await expect(page.locator('.photo-card')).toHaveCount(0);
  await page.getByRole('button', {name:'Reset filters'}).click();
  await expect(page.getByRole('button', {name:'Filter photographs'})).not.toHaveClass(/active/);
  await page.getByRole('textbox', {name:'Filter filename'}).fill('one');
  await expect(page.getByRole('button', {name:'Filter photographs'})).toHaveClass(/active/);
  await page.keyboard.press('Escape');
  await expect(page.getByRole('button', {name:'Filter photographs'})).toHaveAttribute('aria-expanded', 'false');
  await expect(page.locator('.photo-card')).toHaveCount(1);
  await page.getByRole('button', {name:'Filter photographs'}).click();
  await page.getByRole('button', {name:'Reset filters'}).click();
  await page.getByRole('heading', {name:'Library', exact:true}).click();
  await expect(page.getByRole('button', {name:'Filter photographs'})).toHaveAttribute('aria-expanded', 'false');
  await page.getByLabel('Import folder files').setInputFiles(folder);
  await expect(page.locator('.photo-card')).toHaveCount(3);
  await page.setViewportSize({width:390,height:844});
  expect(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth)).toBe(true);
});
