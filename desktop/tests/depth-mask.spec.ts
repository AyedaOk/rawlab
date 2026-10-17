import { test, expect, type Page } from '@playwright/test';
import { depthRangeMask } from '../src/mask';

async function nativeDepth(page: Page) {
  await page.addInitScript(() => {
    const png = 'iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAAC0lEQVR4nGP4DwQACfsD/fteaysAAAAASUVORK5CYII=';
    const preview = {version:1,width:960,height:640,png,quality:'fast',metadata:{rawWidth:960,rawHeight:640}};
    let release: (() => void) | undefined;
    Object.assign(window, {isTauri:true, __depthCalls:0, __holdDepth:false, __failDepth:false,
      __releaseDepth:()=>release?.(),
      __TAURI_INTERNALS__:{invoke:async (command:string,args:any) => {
        if(command === 'external_session' || command === 'supersede_previews') return null;
        if(command === 'raw_source_version') return 'test-source';
        if(command === 'processing_models') return {models:{denoise:[{id:'hf:rawdenoise-okeinla',name:'Denoise',available:true}],
          sharpen:[],depth:[{id:'small',name:'Depth Anything V2 Small',available:true}],activeDepthModelId:'small',provider:'cpu'}};
        if(command === 'render_raw_preview' || command === 'render_processed') return preview;
        if(command === 'process_raw') {
          if(args.points) return {mask:png};
          if(args.operation === 'depth') {
            (window as any).__depthCalls++;
            if((window as any).__holdDepth) await new Promise<void>(resolve => {release=resolve;});
            if((window as any).__failDepth) throw new Error('Depth model weights are missing');
            const canvas=document.createElement('canvas'); canvas.width=256; canvas.height=16;
            const context=canvas.getContext('2d')!, image=context.createImageData(256,16);
            for(let y=0;y<16;y++) for(let x=0;x<256;x++) {
              const i=(y*256+x)*4, value=x*257;
              image.data[i]=value>>8; image.data[i+1]=value&255; image.data[i+3]=255;
            }
            context.putImageData(image,0,0);
            return {key:'test-depth',png:canvas.toDataURL('image/png').split(',')[1],modelId:'small',sourceVersion:'test-source'};
          }
          (window as any).__processedRecipe=structuredClone(args.recipe);
          return {...preview,resultId:'processed-depth'};
        }
        if(command === 'desktop_control') {
          const r=args.request;
          if(r.operation === 'library_list') return {paths:['/photos/depth.ARW','/photos/other.ARW']};
          if(r.operation === 'preset_list') return {names:[]};
          if(r.operation === 'library_metadata' || r.operation === 'library_metadata_save') return {rating:0,labels:[]};
          if(r.operation === 'recipe_load') return {recipe:JSON.parse(localStorage.getItem(r.path) ?? 'null')};
          if(r.operation === 'recipe_save') {localStorage.setItem(r.path,JSON.stringify(r.recipe));return {saved:true};}
        }
        throw new Error(`Unexpected command ${command}`);
      }}});
  });
}

test('depth fades cover hard endpoints and resize depth before selecting', () => {
  const depth={width:9,height:1,values:Float32Array.from([0,.125,.25,.375,.5,.625,.75,.875,1])};
  expect([...depthRangeMask(depth,9,1,[12.5,37.5,62.5,87.5])]).toEqual([0,0,.5,1,1,1,.5,0,0]);
  expect([...depthRangeMask(depth,9,1,[0,0,100,100])]).toEqual(Array(9).fill(1));
  expect([...depthRangeMask(depth,9,1,[50,50,50,50])]).toEqual([0,0,0,0,1,0,0,0,0]);
  expect([...depthRangeMask({width:2,height:1,values:Float32Array.from([0,1])},4,1,[0,0,25,75])]).toEqual([1,1,0,0]);
  expect(() => depthRangeMask(depth,9,1,[0,70,30,100])).toThrow();
});

test('depth fades survive a retained Feather setting', async ({page}) => {
  await nativeDepth(page);
  await page.goto('/');
  await page.getByRole('button',{name:'Select depth.ARW',exact:true}).dblclick();
  await page.getByRole('button',{name:'Add mask',exact:true}).click();
  await page.getByRole('button',{name:'Brush',exact:true}).click();
  await page.getByRole('slider',{name:'Feather',exact:true}).fill('100');
  await page.getByRole('button',{name:'Depth',exact:true}).click();
  const end=page.getByRole('slider',{name:'Depth fully selected end',exact:true});
  const fade=page.getByRole('slider',{name:'Depth fade-out end',exact:true});
  await end.fill('20'); await fade.fill('80');
  const alpha = () => page.locator('.mask-overlay').evaluate((c:HTMLCanvasElement) =>
    c.getContext('2d')!.getImageData(Math.floor(c.width*.3),Math.floor(c.height/2),1,1).data[3]);
  await expect.poll(alpha).toBeGreaterThanOrEqual(84);
  await expect.poll(alpha).toBeLessThanOrEqual(86);
  await end.fill('40'); await fade.fill('60');
  await expect.poll(alpha).toBe(102);
  await page.getByRole('button',{name:'Brush',exact:true}).click();
  await expect(page.getByRole('slider',{name:'Feather',exact:true})).toHaveCount(0);
  await expect(page.getByRole('slider',{name:'Blur',exact:true})).toBeVisible();
});

test('depth ranges preview while dragging, undo once, refine and survive reopening', async ({page}, testInfo) => {
  await nativeDepth(page);
  const errors: string[]=[];
  page.on('pageerror', e => errors.push(e.message));
  await page.goto('/');
  await page.getByRole('button',{name:'Select depth.ARW',exact:true}).dblclick();
  await page.getByRole('button',{name:'Add mask',exact:true}).click();
  await page.getByRole('button',{name:'Depth',exact:true}).click();
  const start=page.getByRole('slider',{name:'Depth fade-in start',exact:true});
  const fullStart=page.getByRole('slider',{name:'Depth fully selected start',exact:true});
  const end=page.getByRole('slider',{name:'Depth fully selected end',exact:true});
  const fadeEnd=page.getByRole('slider',{name:'Depth fade-out end',exact:true});
  await expect(fadeEnd).toHaveValue('65');
  const alpha = (x:number) => page.locator('.mask-overlay').evaluate((c:HTMLCanvasElement,x) =>
    c.getContext('2d')!.getImageData(Math.floor((c.width-1)*x),Math.floor(c.height/2),1,1).data[3],x);
  await expect.poll(() => alpha(.1)).toBe(102);
  await expect.poll(() => alpha(.95)).toBe(0);
  await end.fill('25');
  const bounds=(await fadeEnd.boundingBox())!;
  await page.mouse.move(bounds.x+7+(bounds.width-14)*.65,bounds.y+bounds.height/2);
  await page.mouse.down();
  await page.mouse.move(bounds.x+7+(bounds.width-14)*.95,bounds.y+bounds.height/2,{steps:16});
  await expect.poll(() => alpha(.75)).toBeGreaterThan(15);
  await page.mouse.up();
  await expect(fadeEnd).toHaveValue('95');
  await page.getByRole('button',{name:'Undo',exact:true}).click();
  await expect(fadeEnd).toHaveValue('65');
  await page.getByRole('button',{name:'Redo',exact:true}).click();
  await expect(fadeEnd).toHaveValue('95');
  await fullStart.fill('20');
  await start.focus(); await page.keyboard.press('ArrowRight');
  await expect(start).toHaveValue('1');
  await end.fill('10'); // The end cannot cross the fully-selected start.
  await expect(end).toHaveValue('20');
  await end.fill('50');
  await expect.poll(() => alpha(.3)).toBe(102);
  const show=page.getByRole('switch',{name:'Show mask',exact:true});
  await show.click(); await expect(page.locator('.mask-overlay')).toBeHidden();
  await show.click();
  const invert=page.getByRole('switch',{name:'Invert mask',exact:true});
  await invert.click(); await expect.poll(() => alpha(.3)).toBe(0);
  await invert.click(); await expect.poll(() => alpha(.3)).toBe(102);
  await page.getByRole('button',{name:'Erase',exact:true}).click();
  const photo=(await page.locator('.photo-frame').boundingBox())!;
  await page.mouse.click(photo.x+photo.width*.3,photo.y+photo.height*.5);
  await expect.poll(() => alpha(.3)).toBe(0);
  await page.getByRole('button',{name:'Undo',exact:true}).click();
  await expect.poll(() => alpha(.3)).toBe(102);
  await page.getByRole('group',{name:'Mask tools'}).getByRole('button',{name:'Point',exact:true}).click();
  await page.mouse.click(photo.x+photo.width*.3,photo.y+photo.height*.5);
  await expect.poll(() => alpha(.95)).toBe(102);
  await page.getByRole('button',{name:'Undo',exact:true}).click();
  await expect.poll(() => alpha(.95)).toBe(0);
  await page.getByRole('button',{name:'Depth',exact:true}).click();
  await page.getByRole('button',{name:'Clear mask',exact:true}).click();
  await expect.poll(() => alpha(.3)).toBe(0);
  await expect(page.getByRole('button',{name:'Create depth mask'})).toBeVisible();
  await page.getByRole('button',{name:'Undo',exact:true}).click();
  await expect(fadeEnd).toHaveValue('95');
  expect(await page.evaluate(() => (window as any).__depthCalls)).toBe(1);
  await page.getByRole('button',{name:'Done',exact:true}).click();
  await expect.poll(() => page.evaluate(() => JSON.parse(localStorage.getItem('/photos/depth.ARW') ?? '{}').layers?.[1]?.depth?.range)).toEqual([1,20,50,95]);
  await page.reload();
  await page.getByRole('button',{name:'Select depth.ARW',exact:true}).dblclick();
  await page.getByRole('button',{name:'Mask 1 Local adjustment',exact:true}).click();
  await page.getByRole('button',{name:'Edit Mask',exact:true}).click();
  await page.getByRole('button',{name:'Depth',exact:true}).click();
  await expect(fadeEnd).toHaveValue('95');
  await expect.poll(() => alpha(.3)).toBe(102);
  expect(await page.evaluate(() => (window as any).__depthCalls)).toBe(0);
  await page.getByRole('button',{name:'Done',exact:true}).click();
  await page.getByRole('button',{name:'Update',exact:true}).click();
  await expect.poll(() => page.evaluate(() => (window as any).__processedRecipe?.layers[1].depth.range)).toEqual([1,20,50,95]);
  expect(await page.evaluate(() => !!(window as any).__processedRecipe.depthMaps['test-depth'].png)).toBe(true);
  await page.getByRole('button',{name:'Edit Mask',exact:true}).click();
  await expect(fadeEnd).toHaveValue('95');
  await expect.poll(() => alpha(.3)).toBe(102);
  await page.screenshot({path:testInfo.outputPath('depth-controls.png')});
  expect(errors).toEqual([]);
});

test('depth handles stop on release, move collapsed pairs and undo once per drag', async ({page}) => {
  await nativeDepth(page);
  await page.goto('/');
  await page.getByRole('button',{name:'Select depth.ARW',exact:true}).dblclick();
  await page.getByRole('button',{name:'Add mask',exact:true}).click();
  await page.getByRole('button',{name:'Depth',exact:true}).click();
  const handles=page.locator('.depth-controls input');
  await expect(handles).toHaveCount(4);
  const values=() => handles.evaluateAll((inputs:HTMLInputElement[]) => inputs.map(input => +input.value));
  const point=async (index:number,value:number) => {
    const bounds=(await handles.nth(index).boundingBox())!;
    return {x:bounds.x+7+(bounds.width-14)*value/100,y:bounds.y+bounds.height/2};
  };
  const hover=async (index:number,value:number,thumb=false) => {
    const p=await point(index,value);
    // Coincident arrows share a tip; grab the start on its left or the end on its right.
    await page.mouse.move(p.x+(thumb ? index<2 ? -2 : 2 : 0),p.y,{steps:4});
  };
  const drag=async (index:number,value:number,expected:number[]) => {
    const before=await values();
    await handles.nth((index+1)%4).focus(); // Blurring another handle must not end this new drag.
    await hover(index,before[index],true);
    await page.mouse.down();
    await hover(index,value);
    await expect.poll(values).toEqual(expected); // Live preview, before release.
    const outside=await point(index,value);
    await page.mouse.move(outside.x,outside.y+80);
    await page.mouse.up(); // Release outside the input without losing the undo boundary.
    await hover(index,expected[index]);
    await hover(index,100-expected[index]);
    await expect.poll(values).toEqual(expected); // No button held: hovering must not drag.
    await page.getByRole('button',{name:'Undo',exact:true}).click();
    await expect.poll(values).toEqual(before);
    await page.getByRole('button',{name:'Redo',exact:true}).click();
    await expect.poll(values).toEqual(expected);
  };
  for(let index=0;index<4;index++) await hover(index,30);
  expect(await values()).toEqual([0,0,50,65]);
  await drag(3,85,[0,0,50,85]);
  await drag(0,20,[20,20,50,85]);
  await drag(1,35,[20,35,50,85]);
  await drag(0,10,[10,35,50,85]);
  await drag(2,95,[10,35,95,95]);
  await drag(3,70,[10,35,70,70]);
  await drag(0,90,[70,70,70,70]); // Start cannot pass the end pair.
  await drag(1,20,[20,20,70,70]);
  await handles.nth(0).focus();
  await page.keyboard.press('ArrowRight');
  await expect.poll(values).toEqual([21,21,70,70]);
  await page.getByRole('button',{name:'Undo',exact:true}).click();
  await expect.poll(values).toEqual([20,20,70,70]);
  await hover(3,70,true);
  await page.mouse.down();
  await hover(3,90);
  await expect.poll(values).toEqual([20,20,70,90]);
  await handles.nth(3).dispatchEvent('pointercancel');
  await page.mouse.up();
  await hover(3,50);
  await expect.poll(values).toEqual([20,20,70,70]);
  await page.getByRole('button',{name:'Redo',exact:true}).click();
  await expect.poll(values).toEqual([21,21,70,70]); // Cancelling did not overwrite history.
  const track=await point(2,50);
  await page.mouse.click(track.x,track.y);
  await expect.poll(values).toEqual([21,21,50,70]);
  expect(await page.evaluate(() => (window as any).__depthCalls)).toBe(1);
});

test('depth errors can be retried and late results do not affect another photo', async ({page}) => {
  await nativeDepth(page);
  await page.goto('/');
  await page.getByRole('button',{name:'Select depth.ARW',exact:true}).dblclick();
  await page.getByRole('button',{name:'Add mask',exact:true}).click();
  await page.evaluate(() => { (window as any).__failDepth=true; });
  await page.getByRole('button',{name:'Depth',exact:true}).click();
  const error=page.getByRole('alert',{name:'Depth mask error'});
  await expect(error).toContainText('Depth model weights are missing');
  await expect(page.getByText('Estimating depth…',{exact:true})).toHaveCount(0);
  await page.getByRole('button',{name:'Brush',exact:true}).click();
  await expect(page.locator('.status-message')).toContainText('Draw on the image');
  await page.getByRole('button',{name:'Depth',exact:true}).click();
  await expect(error).toBeVisible();
  await page.getByRole('button',{name:'Dismiss depth error'}).click();
  await expect(error).toHaveCount(0);
  await page.getByRole('button',{name:'Create depth mask'}).click();
  await expect(error).toContainText('Depth model weights are missing');
  await page.evaluate(() => { (window as any).__failDepth=false; (window as any).__holdDepth=true; });
  await page.getByRole('button',{name:'Create depth mask'}).click();
  await expect(error).toHaveCount(0);
  await expect(page.getByText('Estimating depth…', {exact:true}).first()).toBeVisible();
  await page.getByRole('button',{name:'Edit other.ARW',exact:true}).click();
  await page.evaluate(() => (window as any).__releaseDepth());
  await expect(page.getByText('Estimating depth…',{exact:true})).toHaveCount(0);
  await page.getByRole('button',{name:'Edit depth.ARW',exact:true}).click();
  await page.getByRole('button',{name:'Mask 1 Local adjustment',exact:true}).click();
  await page.getByRole('button',{name:'Edit Mask',exact:true}).click();
  await expect(page.getByRole('button',{name:'Clear mask',exact:true})).toBeDisabled();
  await page.evaluate(() => { (window as any).__holdDepth=false; });
  await page.getByRole('button',{name:'Depth',exact:true}).click();
  await expect(page.getByRole('slider',{name:'Depth fade-out end'})).toHaveValue('65');
});

test('a late depth failure does not notify a different photo', async ({page}) => {
  await nativeDepth(page);
  await page.goto('/');
  await page.getByRole('button',{name:'Select depth.ARW',exact:true}).dblclick();
  await page.getByRole('button',{name:'Add mask',exact:true}).click();
  await page.evaluate(() => { (window as any).__holdDepth=true; (window as any).__failDepth=true; });
  await page.getByRole('button',{name:'Depth',exact:true}).click();
  await expect(page.getByText('Estimating depth…',{exact:true}).first()).toBeVisible();
  await page.getByRole('button',{name:'Edit other.ARW',exact:true}).click();
  await page.evaluate(() => (window as any).__releaseDepth());
  await expect(page.getByText('Estimating depth…',{exact:true})).toHaveCount(0);
  await expect(page.getByText(/Depth model weights are missing/)).toHaveCount(0);
});
